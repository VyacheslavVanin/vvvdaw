#include "LV2Instance.h"
#include "LV2UIHost.h"
#include <cstring>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QWidget>
#include <QWindow>
#include <QVBoxLayout>
#include <QTimer>
#include <QGuiApplication>
#include <lv2/atom/atom.h>
#include <lv2/parameters/parameters.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/ui/ui.h>


LV2Instance::LV2Instance() {
}

LV2Instance::~LV2Instance() {
    destroyEditor();
    deactivate();
    if (m_instance) {
        lilv_instance_free(m_instance);
        m_instance = nullptr;
    }
    if (m_ownsWorld && m_world) {
        lilv_world_free(m_world);
        m_world = nullptr;
    }
}

void LV2Instance::buildPortSymbolMap() {
    if (!m_plugin || !m_world) return;
    m_portSymbolMap.clear();
    uint32_t nPorts = lilv_plugin_get_num_ports(m_plugin);
    for (uint32_t i = 0; i < nPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);
        const LilvNode* sym = lilv_port_get_symbol(m_plugin, port);
        if (sym) {
            m_portSymbolMap[lilv_node_as_string(sym)] = i;
        }
    }
    qInfo() << m_name << ": port symbol map has" << m_portSymbolMap.size() << "entries";
}

LV2_URID LV2Instance::uridMapCallback(LV2_URID_Map_Handle handle, const char* uri) {
    auto* inst = static_cast<LV2Instance*>(handle);
    for (auto& [u, id] : inst->m_uridCache) {
        if (u == uri) return id;
    }
    LV2_URID id = static_cast<LV2_URID>(inst->m_uridCache.size() + 1);
    inst->m_uridCache.emplace_back(uri, id);
    return id;
}

int LV2Instance::logSilentVPrintf(LV2_Log_Handle, LV2_URID, const char*, va_list) {
    return 0;
}

LV2_Worker_Status LV2Instance::workerSchedule(LV2_Handle handle, uint32_t size, const void* data) {
    auto* inst = static_cast<LV2Instance*>(handle);
    WorkItem item;
    item.size = size;
    item.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    inst->m_workQueue.push_back(std::move(item));
    return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status LV2Instance::workerRespond(LV2_Worker_Respond_Handle handle, uint32_t size, const void* data) {
    auto* inst = static_cast<LV2Instance*>(handle);
    WorkItem item;
    item.size = size;
    item.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    inst->m_responseQueue.push_back(std::move(item));
    return LV2_WORKER_SUCCESS;
}

void LV2Instance::processWorkQueue() {
    if (m_workQueue.empty()) return;

    // Swap to local so new items queued during processing (e.g. from work_response)
    // stay in m_workQueue for the next cycle instead of being lost.
    std::vector<WorkItem> pending;
    m_workQueue.swap(pending);

    const LV2_Descriptor* desc = lilv_instance_get_descriptor(m_instance);
    LV2_Handle handle = lilv_instance_get_handle(m_instance);
    auto* workerIface = static_cast<const LV2_Worker_Interface*>(
        desc->extension_data(LV2_WORKER__interface));
    if (!workerIface || !workerIface->work) return;

    for (auto& item : pending) {
        m_responseQueue.clear();
        workerIface->work(handle, workerRespond, this,
                          item.size, item.data.data());
        for (auto& resp : m_responseQueue) {
            if (workerIface->work_response)
                workerIface->work_response(handle, resp.size, resp.data.data());
        }
    }

    if (workerIface->end_run)
        workerIface->end_run(handle);
}

bool LV2Instance::load(const QString& path) {
    m_filePath = path;
    m_pluginId = path;

    if (!m_world) {
        m_world = lilv_world_new();
        m_ownsWorld = true;
        lilv_world_load_all(m_world);
    }

    auto* uriNode = lilv_new_uri(m_world, path.toUtf8().constData());
    m_plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(m_world), uriNode);
    lilv_node_free(uriNode);
    if (!m_plugin) return false;

    auto* nameNode = lilv_plugin_get_name(m_plugin);
    m_name = nameNode ? QString::fromUtf8(lilv_node_as_string(nameNode)) : path;
    if (nameNode) lilv_node_free(nameNode);

    auto* authorNode = lilv_plugin_get_author_name(m_plugin);
    m_vendor = authorNode ? QString::fromUtf8(lilv_node_as_string(authorNode)) : QString();
    if (authorNode) lilv_node_free(authorNode);

    m_uridMap.handle = this;
    m_uridMap.map = uridMapCallback;

    m_uridSampleRate = uridMapCallback(this, LV2_PARAMETERS__sampleRate);
    m_uridMaxBlockLength = uridMapCallback(this, "http://lv2plug.in/ns/ext/buf-size#maxBlockLength");
    m_uridAtomSequence = uridMapCallback(this, LV2_ATOM__Sequence);
    m_uridAtomObject = uridMapCallback(this, LV2_ATOM__Object);

    m_uridAtomPath = uridMapCallback(this, LV2_ATOM__Path);
    m_uridAtomString = uridMapCallback(this, LV2_ATOM__String);
    m_uridAtomBlank = uridMapCallback(this, LV2_ATOM__Blank);
    m_uridEventTransfer = uridMapCallback(this, LV2_ATOM__eventTransfer);
    m_uridPatchGet = uridMapCallback(this, LV2_PATCH__Get);
    m_uridPatchSet = uridMapCallback(this, LV2_PATCH__Set);
    m_uridPatchProperty = uridMapCallback(this, LV2_PATCH__property);
    m_uridPatchValue = uridMapCallback(this, LV2_PATCH__value);
    m_uridPatchMessage = uridMapCallback(this, LV2_PATCH__Message);
    m_uridMidiEvent = uridMapCallback(this, LV2_MIDI__MidiEvent);

    LilvNode* instrumentClass = lilv_new_uri(m_world, LV2_CORE__InstrumentPlugin);
    const LilvPluginClass* pluginClass = lilv_plugin_get_class(m_plugin);
    const LilvNode* classUri = pluginClass ? lilv_plugin_class_get_uri(pluginClass) : nullptr;
    m_isInstrument = classUri && lilv_node_equals(classUri, instrumentClass);
    lilv_node_free(instrumentClass);

    m_optionSampleRate = static_cast<float>(m_sampleRate);
    m_optionMaxBlockLength = m_maxBlockSize;

    m_options[0].context = LV2_OPTIONS_INSTANCE;
    m_options[0].subject = 0;
    m_options[0].key = m_uridSampleRate;
    m_options[0].size = sizeof(float);
    m_options[0].type = uridMapCallback(this, LV2_ATOM__Float);
    m_options[0].value = &m_optionSampleRate;

    m_options[1].context = LV2_OPTIONS_INSTANCE;
    m_options[1].subject = 0;
    m_options[1].key = m_uridMaxBlockLength;
    m_options[1].size = sizeof(int32_t);
    m_options[1].type = uridMapCallback(this, LV2_ATOM__Int);
    m_options[1].value = &m_optionMaxBlockLength;

    m_options[2].context = LV2_OPTIONS_INSTANCE;
    m_options[2].subject = 0;
    m_options[2].key = 0;
    m_options[2].size = 0;
    m_options[2].type = 0;
    m_options[2].value = nullptr;

    m_instance = lilv_plugin_instantiate(m_plugin, m_sampleRate, m_features);
    if (!m_instance) return false;

    uint32_t nPorts = lilv_plugin_get_num_ports(m_plugin);
    m_portInfos.resize(nPorts);
    m_ctrlValues.resize(nPorts, 0.0f);

    LilvNode* audioPort = lilv_new_uri(m_world, LV2_CORE__AudioPort);
    LilvNode* controlPort = lilv_new_uri(m_world, LV2_CORE__ControlPort);
    LilvNode* atomPort = lilv_new_uri(m_world, LV2_ATOM__AtomPort);
    LilvNode* inputPort = lilv_new_uri(m_world, LV2_CORE__InputPort);
    LilvNode* outputPort = lilv_new_uri(m_world, LV2_CORE__OutputPort);
    LilvNode* minSizeNode = lilv_new_uri(m_world, LV2_RESIZE_PORT__minimumSize);

    for (uint32_t i = 0; i < nPorts; ++i) {
        const LilvPort* port = lilv_plugin_get_port_by_index(m_plugin, i);
        PortInfo& pi = m_portInfos[i];
        pi.port = port;
        pi.index = static_cast<int>(i);

        LilvNode* portName = lilv_port_get_name(m_plugin, port);
        pi.name = portName ? QString::fromUtf8(lilv_node_as_string(portName))
                           : QString("Port %1").arg(i);
        if (portName) lilv_node_free(portName);

        LilvNode* defNode = nullptr;
        LilvNode* minNode = nullptr;
        LilvNode* maxNode = nullptr;
        lilv_port_get_range(m_plugin, port, &defNode, &minNode, &maxNode);
        pi.defaultValue = defNode ? lilv_node_as_float(defNode) : 0.0f;
        pi.minValue = minNode ? lilv_node_as_float(minNode) : 0.0f;
        pi.maxValue = maxNode ? lilv_node_as_float(maxNode) : 1.0f;
        m_ctrlValues[i] = pi.defaultValue;
        if (defNode) lilv_node_free(defNode);
        if (minNode) lilv_node_free(minNode);
        if (maxNode) lilv_node_free(maxNode);

        bool isAudio = lilv_port_is_a(m_plugin, port, audioPort);
        bool isControl = lilv_port_is_a(m_plugin, port, controlPort);
        bool isAtom = lilv_port_is_a(m_plugin, port, atomPort);
        bool isInput = lilv_port_is_a(m_plugin, port, inputPort);
        bool isOutput = lilv_port_is_a(m_plugin, port, outputPort);

        if (isAudio && isInput) {
            pi.type = PluginPortInfo::Type::Audio;
            pi.direction = PluginPortInfo::Direction::Input;
            m_audioInPorts.push_back(nullptr);
            m_audioInPortIndices.push_back(i);
        } else if (isAudio && isOutput) {
            pi.type = PluginPortInfo::Type::Audio;
            pi.direction = PluginPortInfo::Direction::Output;
            m_audioOutPorts.push_back(nullptr);
            m_audioOutPortIndices.push_back(i);
        } else if (isControl) {
            pi.type = PluginPortInfo::Type::Control;
            pi.direction = isInput ? PluginPortInfo::Direction::Input
                                   : PluginPortInfo::Direction::Output;
            m_ctrlPorts.push_back(&m_ctrlValues[i]);
            m_ctrlPortIndices.push_back(i);
        } else if (isAtom) {
            LilvNode* pathNode = lilv_new_uri(m_world, LV2_ATOM__Path);
            bool isPath = lilv_port_supports_event(m_plugin, port, pathNode);
            lilv_node_free(pathNode);
            LilvNode* stringNode = lilv_new_uri(m_world, LV2_ATOM__String);
            bool isString = lilv_port_supports_event(m_plugin, port, stringNode);
            lilv_node_free(stringNode);
            LilvNode* patchMsgNode = lilv_new_uri(m_world, LV2_PATCH__Message);
            bool isPatchMsg = isInput && lilv_port_supports_event(m_plugin, port, patchMsgNode);
            lilv_node_free(patchMsgNode);
            LilvNode* midiNode = lilv_new_uri(m_world, LV2_MIDI__MidiEvent);
            bool isMidi = lilv_port_supports_event(m_plugin, port, midiNode);
            lilv_node_free(midiNode);

            if (isPatchMsg && isInput) {
                pi.type = PluginPortInfo::Type::Path;
                const LilvNode* pluginUri = lilv_plugin_get_uri(m_plugin);
                LilvNode* patchWritable = lilv_new_uri(m_world, LV2_PATCH__writable);
                LilvNodes* writable = lilv_world_find_nodes(m_world, pluginUri, patchWritable, nullptr);
                lilv_node_free(patchWritable);
                LILV_FOREACH(nodes, n, writable) {
                    const LilvNode* propNode = lilv_nodes_get(writable, n);
                    const char* propUri = lilv_node_as_string(propNode);
                    LV2_URID propUrid = uridMapCallback(this, propUri);
                    m_portPropertyURIDs[i] = propUrid;

                    LilvNode* rdfsLabel = lilv_new_uri(m_world, "http://www.w3.org/2000/01/rdf-schema#label");
                    LilvNode* labelNode = lilv_world_get(m_world, propNode, rdfsLabel, nullptr);
                    if (labelNode) {
                        pi.name = QString::fromUtf8(lilv_node_as_string(labelNode));
                        lilv_node_free(labelNode);
                    }
                    lilv_node_free(rdfsLabel);

                    LilvNode* rdfsRange = lilv_new_uri(m_world, "http://www.w3.org/2000/01/rdf-schema#range");
                    LilvNode* rangeNode = lilv_world_get(m_world, propNode, rdfsRange, nullptr);
                    if (rangeNode) {
                        LilvNode* atomPathUri = lilv_new_uri(m_world, LV2_ATOM__Path);
                        if (lilv_node_equals(rangeNode, atomPathUri))
                            pi.type = PluginPortInfo::Type::Path;
                        else
                            pi.type = PluginPortInfo::Type::String;
                        lilv_node_free(atomPathUri);
                        lilv_node_free(rangeNode);
                    }
                    lilv_node_free(rdfsRange);

                    qInfo() << m_name << ": patch property" << propUri
                             << "-> \"" << pi.name << "\""
                             << (pi.type == PluginPortInfo::Type::Path ? "Path" : "String");
                }
                lilv_nodes_free(writable);
            } else if (isPath) {
                pi.type = PluginPortInfo::Type::Path;
                qInfo() << m_name << ": port" << i << pi.name << "-> Path";
            } else if (isString) {
                pi.type = PluginPortInfo::Type::String;
                qInfo() << m_name << ": port" << i << pi.name << "-> String";
            } else {
                pi.type = PluginPortInfo::Type::Atom;
                qInfo() << m_name << ": port" << i << pi.name << "-> Atom (fallback)";
            }
            pi.direction = isInput ? PluginPortInfo::Direction::Input
                                   : PluginPortInfo::Direction::Output;

            LilvNode* minSizeNodeVal = lilv_port_get(m_plugin, port, minSizeNode);
            uint32_t minSize = 2048;
            if (minSizeNodeVal) {
                minSize = static_cast<uint32_t>(lilv_node_as_int(minSizeNodeVal));
                lilv_node_free(minSizeNodeVal);
            }
            m_atomPorts.push_back({i, minSize, isInput, isMidi});
        } else {
            pi.type = PluginPortInfo::Type::Control;
            pi.direction = isInput ? PluginPortInfo::Direction::Input
                                   : PluginPortInfo::Direction::Output;
        }

        if (isControl)
            lilv_instance_connect_port(m_instance, i, &m_ctrlValues[i]);
        else if (!isAtom)
            lilv_instance_connect_port(m_instance, i, nullptr);
    }

    m_audioInBuffers.resize(m_audioInPorts.size(), std::vector<float>(m_maxBlockSize, 0.0f));
    m_audioOutBuffers.resize(m_audioOutPorts.size(), std::vector<float>(m_maxBlockSize, 0.0f));

    for (size_t j = 0; j < m_audioInPorts.size(); ++j) {
        m_audioInPorts[j] = m_audioInBuffers[j].data();
        lilv_instance_connect_port(m_instance, m_audioInPortIndices[j], m_audioInPorts[j]);
    }
    for (size_t j = 0; j < m_audioOutPorts.size(); ++j) {
        m_audioOutPorts[j] = m_audioOutBuffers[j].data();
        lilv_instance_connect_port(m_instance, m_audioOutPortIndices[j], m_audioOutPorts[j]);
    }

    m_atomBuffers.resize(m_atomPorts.size());
    for (size_t j = 0; j < m_atomPorts.size(); ++j) {
        uint32_t bufSize = m_atomPorts[j].minSize;
        if (bufSize < sizeof(LV2_Atom_Sequence) + sizeof(LV2_Atom))
            bufSize = sizeof(LV2_Atom_Sequence) + sizeof(LV2_Atom);
        m_atomBuffers[j].resize(bufSize, 0);
        auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(m_atomBuffers[j].data());
        seq->atom.size = bufSize - sizeof(LV2_Atom);
        seq->atom.type = 0;
        seq->body.unit = 0;
        seq->body.pad = 0;
        m_atomPortPtrs.push_back(m_atomBuffers[j].data());
        lilv_instance_connect_port(m_instance, m_atomPorts[j].index, m_atomPortPtrs.back());
    }

    lilv_node_free(audioPort);
    lilv_node_free(controlPort);
    lilv_node_free(atomPort);
    lilv_node_free(inputPort);
    lilv_node_free(outputPort);
    lilv_node_free(minSizeNode);

    // Detect UI
    LilvNode* uiX11 = lilv_new_uri(m_world, "http://lv2plug.in/ns/extensions/ui#X11UI");
    const LilvUIs* uis = lilv_plugin_get_uis(m_plugin);

    LILV_FOREACH(uis, uiIter, uis) {
        const LilvUI* ui = lilv_uis_get(uis, uiIter);
        if (lilv_ui_is_a(ui, uiX11)) {
            const LilvNode* uiUriNode = lilv_ui_get_uri(ui);
            const LilvNode* uiBundleNode = lilv_ui_get_bundle_uri(ui);
            const LilvNode* uiBinaryNode = lilv_ui_get_binary_uri(ui);

            m_uiUri = QString::fromUtf8(lilv_node_as_string(uiUriNode));

            char* bundlePath = lilv_file_uri_parse(lilv_node_as_string(uiBundleNode), nullptr);
            m_uiBundlePath = bundlePath ? QString::fromUtf8(bundlePath) : QString();
            free(bundlePath);

            char* binaryPath = lilv_file_uri_parse(lilv_node_as_string(uiBinaryNode), nullptr);
            m_uiBinaryPath = binaryPath ? QString::fromUtf8(binaryPath) : QString();
            free(binaryPath);

            qInfo() << m_name << ": found X11 UI" << m_uiUri
                     << "bundle:" << m_uiBundlePath << "binary:" << m_uiBinaryPath;
            break;
        }
    }

    if (m_uiUri.isEmpty())
        qInfo() << m_name << ": no X11 UI found";

    lilv_node_free(uiX11);

    return true;
}

bool LV2Instance::activate(double sampleRate, int maxBlockSize) {
    if (!m_instance) return false;

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_optionSampleRate = static_cast<float>(sampleRate);
    m_optionMaxBlockLength = maxBlockSize;

    for (auto& buf : m_audioInBuffers)
        buf.resize(maxBlockSize, 0.0f);
    for (auto& buf : m_audioOutBuffers)
        buf.resize(maxBlockSize, 0.0f);

    for (size_t i = 0; i < m_audioInPorts.size(); ++i)
        lilv_instance_connect_port(m_instance, m_audioInPortIndices[i], m_audioInBuffers[i].data());
    for (size_t i = 0; i < m_audioOutPorts.size(); ++i)
        lilv_instance_connect_port(m_instance, m_audioOutPortIndices[i], m_audioOutBuffers[i].data());

    lilv_instance_activate(m_instance);
    m_active = true;

    for (size_t j = 0; j < m_atomPorts.size(); ++j) {
        if (m_atomPorts[j].isInput)
            requestPatchGet(static_cast<int>(j));
    }

    return true;
}

bool LV2Instance::deactivate() {
    if (!m_active || !m_instance) return true;
    lilv_instance_deactivate(m_instance);
    m_active = false;
    return true;
}

bool LV2Instance::process(float** inputBuffers, float** outputBuffers,
                          int numSamples, int numChannels, const MidiBuffer* midi) {
    if (!m_active || !m_instance || !m_enabled) {
        if (outputBuffers && inputBuffers) {
            for (int ch = 0; ch < numChannels; ++ch)
                std::memcpy(outputBuffers[ch], inputBuffers[ch], numSamples * sizeof(float));
        }
        return true;
    }

    int samples = std::min(numSamples, m_maxBlockSize);

    for (size_t i = 0; i < m_audioInPorts.size(); ++i) {
        int ch = static_cast<int>(i);
        if (m_audioInPorts.size() == 1 && numChannels >= 2 && inputBuffers &&
            !m_audioInBuffers.empty()) {
            for (int s = 0; s < samples; ++s)
                m_audioInBuffers[0][static_cast<size_t>(s)] =
                    (inputBuffers[0][s] + inputBuffers[1][s]) * 0.5f;
            m_audioInPorts[0] = m_audioInBuffers[0].data();
            lilv_instance_connect_port(m_instance, m_audioInPortIndices[0], m_audioInPorts[0]);
            continue;
        }
        bool mapped = (ch < numChannels && inputBuffers);
        m_audioInPorts[i] = mapped ? inputBuffers[ch] : m_audioInBuffers[i].data();
        if (!mapped)
            std::memset(m_audioInPorts[i], 0, samples * sizeof(float));
        lilv_instance_connect_port(m_instance, m_audioInPortIndices[i], m_audioInPorts[i]);
    }

    for (size_t i = 0; i < m_audioOutPorts.size(); ++i) {
        int ch = static_cast<int>(i);
        m_audioOutPorts[i] = (ch < numChannels && outputBuffers) ? outputBuffers[ch] : m_audioOutBuffers[i].data();
        lilv_instance_connect_port(m_instance, m_audioOutPortIndices[i], m_audioOutPorts[i]);
    }

    for (size_t i = 0; i < m_ctrlPorts.size(); ++i)
        lilv_instance_connect_port(m_instance, m_ctrlPortIndices[i], m_ctrlPorts[i]);

    // Reset all atom buffers before run(). For output ports the atom size
    // advertises the buffer capacity (the plugin overwrites it), otherwise
    // the plugin cannot know how much it may write (e.g. SiSco's notify port).
    for (size_t j = 0; j < m_atomPorts.size(); ++j) {
        auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(m_atomBuffers[j].data());
        seq->atom.type = m_uridAtomSequence;
        seq->body.unit = 0;
        seq->body.pad = 0;
        if (m_atomPorts[j].isInput) {
            seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
        } else {
            seq->atom.size = static_cast<uint32_t>(m_atomBuffers[j].size());
        }
    }

    // Drain UI -> plugin atom messages (queued on the GUI thread).
    std::deque<UiMessage> uiMessages;
    {
        std::lock_guard<std::mutex> lock(m_uiMutex);
        uiMessages.swap(m_uiToPlugin);
    }

    // Forge pending patch messages, UI messages and MIDI events into input atom
    // buffers just before run()
    for (size_t j = 0; j < m_atomPorts.size(); ++j) {
        if (!m_atomPorts[j].isInput) continue;
        int portIdx = static_cast<int>(m_atomPorts[j].index);
        auto& buf = m_atomBuffers[j];

        bool doGet = m_pendingPatchGet && m_pendingPortIndex == portIdx;
        bool doSet = m_pendingPatchSet && m_pendingPortIndex == portIdx;
        bool hasMidi = m_atomPorts[j].isMidi && midi && !midi->empty();
        std::vector<UiMessage> portUiMsgs;
        for (auto& msg : uiMessages) {
            if (static_cast<int>(msg.portIndex) == portIdx)
                portUiMsgs.push_back(std::move(msg));
        }
        if (!doGet && !doSet && !hasMidi && portUiMsgs.empty()) continue;

        if (buf.size() < sizeof(LV2_Atom_Sequence) + 256) continue;

        LV2_Atom_Forge forge;
        lv2_atom_forge_init(&forge, &m_uridMap);
        lv2_atom_forge_set_buffer(&forge, buf.data(), static_cast<uint32_t>(buf.size()));

        LV2_Atom_Forge_Frame seq_frame;
        lv2_atom_forge_sequence_head(&forge, &seq_frame, 0);

        if (doGet) {
            lv2_atom_forge_frame_time(&forge, 0);
            LV2_Atom_Forge_Frame obj_frame;
            lv2_atom_forge_object(&forge, &obj_frame, 0, m_uridPatchGet);
            lv2_atom_forge_pop(&forge, &obj_frame);
        }

        if (doSet) {
            if (m_pendingValue.isEmpty() && m_pendingIsPath) continue;
            auto propIt = m_portPropertyURIDs.find(portIdx);
            if (propIt != m_portPropertyURIDs.end()) {
                lv2_atom_forge_frame_time(&forge, 0);
                LV2_Atom_Forge_Frame obj_frame;
                lv2_atom_forge_object(&forge, &obj_frame, 0, m_uridPatchSet);

                lv2_atom_forge_key(&forge, m_uridPatchProperty);
                lv2_atom_forge_urid(&forge, propIt->second);

                lv2_atom_forge_key(&forge, m_uridPatchValue);
                QByteArray utf8 = m_pendingValue.toUtf8();
                if (m_pendingIsPath) {
                    lv2_atom_forge_atom(&forge, static_cast<uint32_t>(utf8.size()) + 1, m_uridAtomPath);
                    lv2_atom_forge_write(&forge, utf8.constData(), static_cast<uint32_t>(utf8.size()) + 1);
                } else {
                    lv2_atom_forge_atom(&forge, static_cast<uint32_t>(utf8.size()), m_uridAtomString);
                    lv2_atom_forge_write(&forge, utf8.constData(), static_cast<uint32_t>(utf8.size()));
                }

                lv2_atom_forge_pop(&forge, &obj_frame);
            }
        }

        for (auto& msg : portUiMsgs) {
            const auto* atom = reinterpret_cast<const LV2_Atom*>(msg.data.constData());
            uint32_t total = lv2_atom_total_size(atom);
            if (total < sizeof(LV2_Atom) ||
                forge.offset + total + 8 > forge.size)
                continue;
            lv2_atom_forge_frame_time(&forge, 0);
            lv2_atom_forge_raw(&forge, atom, total);
            lv2_atom_forge_pad(&forge, total);
        }

        if (hasMidi) {
            for (const auto& m : *midi) {
                lv2_atom_forge_frame_time(&forge, m.sampleOffset);
                LV2_Atom atom;
                atom.size = 3;
                atom.type = m_uridMidiEvent;
                lv2_atom_forge_raw(&forge, &atom, sizeof(LV2_Atom));
                uint8_t midiBytes[3] = { m.status, m.data1, m.data2 };
                lv2_atom_forge_raw(&forge, midiBytes, 3);
                lv2_atom_forge_pad(&forge, sizeof(LV2_Atom) + 3);
            }
        }

        lv2_atom_forge_pop(&forge, &seq_frame);
    }

    m_pendingPatchGet = false;
    m_pendingPatchSet = false;

    lilv_instance_run(m_instance, samples);

    // Mono plugin: duplicate the single output channel so downstream mixing
    // sees a centered stereo signal instead of a hard-panned-left one.
    if (m_audioOutPorts.size() == 1 && outputBuffers && numChannels >= 2) {
        const float* src = m_audioOutPorts[0];
        if (src) {
            float* dst = outputBuffers[1];
            std::memcpy(dst, src, static_cast<size_t>(samples) * sizeof(float));
        }
    }

    processWorkQueue();

    readOutputAtoms();

    return true;
}

QString LV2Instance::name() const { return m_name; }
QString LV2Instance::vendor() const { return m_vendor; }
QString LV2Instance::pluginId() const { return m_pluginId; }
QString LV2Instance::filePath() const { return m_filePath; }
bool LV2Instance::isActive() const { return m_active; }
void LV2Instance::setEnabled(bool enabled) { m_enabled = enabled; }
bool LV2Instance::isEnabled() const { return m_enabled; }

void LV2Instance::requestPatchGet(int atomBufIndex) {
    if (atomBufIndex < 0 || atomBufIndex >= static_cast<int>(m_atomPorts.size())) return;
    if (!m_atomPorts[atomBufIndex].isInput) return;
    m_pendingPatchGet = true;
    m_pendingPortIndex = static_cast<int>(m_atomPorts[atomBufIndex].index);
}

void LV2Instance::readOutputAtoms() {
    for (size_t j = 0; j < m_atomPorts.size(); ++j) {
        if (m_atomPorts[j].isInput) continue;

        auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(m_atomBuffers[j].data());

        LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
            // Forward analyser/notify messages (atom Object/Blank events) to
            // the UI on the GUI thread; the UI expects one port_event per
            // message. patching/string-param handling below is unchanged.
            if (m_uiHost &&
                (ev->body.type == m_uridAtomObject || ev->body.type == m_uridAtomBlank)) {
                uint32_t total = sizeof(LV2_Atom) + ev->body.size;
                std::lock_guard<std::mutex> lock(m_uiMutex);
                if (m_pluginToUi.size() < 512) {
                    UiMessage msg;
                    msg.portIndex = m_atomPorts[j].index;
                    msg.data = QByteArray(reinterpret_cast<const char*>(&ev->body),
                                          static_cast<int>(total));
                    m_pluginToUi.push_back(std::move(msg));
                }
            }

            if (ev->body.type == m_uridAtomObject) {
                auto* obj = reinterpret_cast<LV2_Atom_Object*>(&ev->body);
                if (obj->body.otype != m_uridPatchSet) continue;

                const LV2_Atom* property = nullptr;
                const LV2_Atom* value = nullptr;
                LV2_ATOM_OBJECT_FOREACH(obj, prop) {
                    if (prop->key == m_uridPatchProperty)
                        property = &prop->value;
                    else if (prop->key == m_uridPatchValue)
                        value = &prop->value;
                }
                if (!property || !value) continue;

                int portIndex = static_cast<int>(m_atomPorts[j].index);

                QString strValue;
                if (value->type == m_uridAtomPath) {
                    auto* pathData = static_cast<const char*>(LV2_ATOM_BODY(value));
                    uint32_t pathLen = value->size;
                    if (pathLen > 0) {
                        pathLen = strnlen(pathData, pathLen);
                        strValue = QString::fromUtf8(pathData, static_cast<int>(pathLen));
                    }
                } else if (value->type == m_uridAtomString) {
                    auto* strData = static_cast<const char*>(LV2_ATOM_BODY(value));
                    strValue = QString::fromUtf8(strData, static_cast<int>(value->size));
                }

                if (!strValue.isEmpty()) {
                    QString oldValue = m_stringParams[portIndex];
                    if (oldValue != strValue) {
                        m_stringParams[portIndex] = strValue;
                        auto* propBody = static_cast<const LV2_Atom_URID*>(LV2_ATOM_BODY(property));
                        m_portPropertyURIDs[portIndex] = propBody->body;

                        if (m_stringParamChangeCallback) {
                            QTimer::singleShot(0, [this, portIndex, oldValue, strValue]() {
                                if (m_stringParamChangeCallback)
                                    m_stringParamChangeCallback(portIndex, oldValue, strValue);
                            });
                        }
                    }
                }
            }
        }
    }
}

void LV2Instance::drainUiEvents() {
    if (!m_uiHost) return;
    std::deque<UiMessage> msgs;
    {
        std::lock_guard<std::mutex> lock(m_uiMutex);
        msgs.swap(m_pluginToUi);
    }
    for (auto& msg : msgs) {
        if (msg.data.size() >= static_cast<int>(sizeof(LV2_Atom)))
            m_uiHost->sendAtomEvent(static_cast<int>(msg.portIndex),
                                    static_cast<uint32_t>(msg.data.size()),
                                    m_uridEventTransfer,
                                    msg.data.constData());
    }
}

void LV2Instance::sendPatchSet(int portIndex, const QString& value, bool isPath) {
    auto propIt = m_portPropertyURIDs.find(portIndex);
    if (propIt == m_portPropertyURIDs.end()) {
        qWarning() << m_name << ": no patch:property URID known for port" << portIndex << "- cannot send patch:Set";
        return;
    }
    m_pendingPatchSet = true;
    m_pendingPortIndex = portIndex;
    m_pendingValue = value;
    m_pendingIsPath = isPath;
}

int LV2Instance::latencySamples() const {
    if (!m_plugin || !m_world) return 0;
    LilvNode* latencySymbol = lilv_new_uri(m_world, LV2_CORE__latency);
    const LilvPort* port = lilv_plugin_get_port_by_symbol(m_plugin, latencySymbol);
    lilv_node_free(latencySymbol);
    if (port) {
        uint32_t idx = lilv_port_get_index(m_plugin, port);
        if (idx < m_ctrlValues.size())
            return static_cast<int>(m_ctrlValues[idx]);
    }
    return 0;
}

std::vector<PluginPortInfo> LV2Instance::ports() const {
    std::vector<PluginPortInfo> result;
    for (auto& pi : m_portInfos) {
        PluginPortInfo info;
        info.type = pi.type;
        info.direction = pi.direction;
        info.name = pi.name;
        info.index = pi.index;
        info.defaultValue = pi.defaultValue;
        info.minValue = pi.minValue;
        info.maxValue = pi.maxValue;
        result.push_back(info);
    }
    return result;
}

void LV2Instance::setParameter(int index, float value) {
    if (index < 0 || index >= static_cast<int>(m_ctrlValues.size())) return;
    if (m_ctrlValues[index] == value) return;
    m_ctrlValues[index] = value;
    if (m_uiHost)
        m_uiHost->sendPortEvent(index, value);
    if (m_paramValueCallback)
        m_paramValueCallback(index, value);
}

float LV2Instance::getParameter(int index) const {
    if (index < 0 || index >= static_cast<int>(m_ctrlValues.size())) return 0.0f;
    return m_ctrlValues[index];
}

QString LV2Instance::getStringParameter(int index) const {
    auto it = m_stringParams.find(index);
    return it != m_stringParams.end() ? it->second : QString();
}

void LV2Instance::setStringParameter(int index, const QString& value) {
    auto it = m_stringParams.find(index);
    QString oldValue = it != m_stringParams.end() ? it->second : QString();
    if (oldValue == value) return;

    m_stringParams[index] = value;
    sendPatchSet(index, value, true);
    if (m_stringParamValueCallback)
        m_stringParamValueCallback(index, value);
}

QString LV2Instance::parameterPropertyUri(int index) const {
    auto it = m_portPropertyURIDs.find(index);
    if (it == m_portPropertyURIDs.end()) return {};
    // Reverse-lookup the URID to URI string
    for (auto& [uri, id] : m_uridCache) {
        if (id == it->second)
            return QString::fromStdString(uri);
    }
    return {};
}

bool LV2Instance::hasEditor() const {
    return !m_ctrlPortIndices.empty() || !m_atomPorts.empty();
}

void* LV2Instance::createEditor(void* parentWindow) {
    if (m_uiUri.isEmpty()) return nullptr;
    if (m_uiHost) return m_uiContainer;

    if (!QGuiApplication::platformName().contains("xcb")) {
        qWarning() << m_name << ": LV2 X11 UI requires X11 (QT_QPA_PLATFORM=xcb)";
        return nullptr;
    }

    m_uiHost = std::make_unique<LV2UIHost>();
    buildPortSymbolMap();
    m_uiHost->setPortMap(m_portSymbolMap);
    m_uiHost->portWriteCallback = [this](int index, float value) {
        float oldValue = getParameter(index);
        if (oldValue != value && m_paramChangeCallback)
            m_paramChangeCallback(index, oldValue, value);
        else
            setParameter(index, value);
    };
    m_uiHost->atomWriteCallback = [this](int portIndex, uint32_t, uint32_t,
                                          uint32_t protocol, const void* buffer) {
        if (protocol == 0) return;
        const LV2_Atom* atom = static_cast<const LV2_Atom*>(buffer);
        if (!atom) return;

        // Forward the message to the plugin's input atom port (e.g. SiSco's
        // ui_on/ui_state/ui_off) on the next process cycle.
        uint32_t total = lv2_atom_total_size(atom);
        if (total >= sizeof(LV2_Atom)) {
            std::lock_guard<std::mutex> lock(m_uiMutex);
            if (m_uiToPlugin.size() < 128) {
                UiMessage msg;
                msg.portIndex = static_cast<uint32_t>(portIndex);
                msg.data = QByteArray(reinterpret_cast<const char*>(atom), static_cast<int>(total));
                m_uiToPlugin.push_back(std::move(msg));
            }
        }

        if (atom->type == m_uridAtomObject) {
            auto* obj = reinterpret_cast<const LV2_Atom_Object*>(atom);
            if (obj->body.otype != m_uridPatchSet) return;

            const LV2_Atom* property = nullptr;
            const LV2_Atom* value = nullptr;
            LV2_ATOM_OBJECT_FOREACH(obj, prop) {
                if (prop->key == m_uridPatchProperty)
                    property = &prop->value;
                else if (prop->key == m_uridPatchValue)
                    value = &prop->value;
            }
            if (!property || !value) return;

            QString strValue;
            if (value->type == m_uridAtomPath) {
                auto* pathData = static_cast<const char*>(LV2_ATOM_BODY(value));
                uint32_t pathLen = strnlen(pathData, value->size);
                strValue = QString::fromUtf8(pathData, static_cast<int>(pathLen));
            } else if (value->type == m_uridAtomString) {
                auto* strData = static_cast<const char*>(LV2_ATOM_BODY(value));
                strValue = QString::fromUtf8(strData, static_cast<int>(value->size));
            }

            if (!strValue.isEmpty()) {
                QString oldValue = m_stringParams[portIndex];
                if (oldValue != strValue) {
                    m_stringParams[portIndex] = strValue;
                    if (m_stringParamChangeCallback)
                        m_stringParamChangeCallback(portIndex, oldValue, strValue);
                }
            }
        }
    };

    auto* pluginUriNode = lilv_new_uri(m_world, m_pluginId.toUtf8().constData());
    QString pluginUri = lilv_node_as_string(pluginUriNode);
    lilv_node_free(pluginUriNode);

    unsigned long parentXid = 0;
    if (parentWindow) {
        auto* container = static_cast<QWidget*>(parentWindow);
        parentXid = container->winId();
    }

    if (!m_uiHost->open(pluginUri.toUtf8().constData(),
                        m_uiBundlePath.toUtf8().constData(),
                        m_uiBinaryPath.toUtf8().constData(),
                        &m_uridMap, m_options, m_instance, parentXid)) {
        qWarning() << m_name << ": LV2UIHost::open failed";
        m_uiHost.reset();
        m_uiContainer = nullptr;
        return nullptr;
    }

    m_idleTimer = new QTimer;
    QObject::connect(m_idleTimer, &QTimer::timeout, m_idleTimer, [this]() {
        if (!m_uiHost) return;
        if (m_uiHost->hasIdleInterface())
            m_uiHost->idle();
        drainUiEvents();
    });
    m_idleTimer->start(16);

    // Sync current parameter values to the freshly-created UI
    for (int idx : m_ctrlPortIndices)
        m_uiHost->sendPortEvent(idx, m_ctrlValues[idx]);

    qInfo() << m_name << ": LV2 X11 UI created (embedded, parent=" << parentXid << ")";
    return (void*)1;
}

void LV2Instance::destroyEditor() {
    if (m_idleTimer) {
        m_idleTimer->stop();
        m_idleTimer->deleteLater();
        m_idleTimer = nullptr;
    }
    if (m_uiContainer) {
        m_uiContainer->setParent(nullptr);
        m_uiContainer->deleteLater();
        m_uiContainer = nullptr;
    }
    if (m_uiHost) {
        m_uiHost->close();
        m_uiHost.reset();
    }
}

void LV2Instance::resizeEditor(int, int) {
}

bool LV2Instance::getEditorSize(int& width, int& height) const {
    if (!m_uiHost) return false;
    return m_uiHost->getChildSize(width, height);
}

QJsonObject LV2Instance::stateToJson() const {
    QJsonObject json;
    json["type"] = "lv2";
    json["path"] = m_filePath;
    json["pluginId"] = m_pluginId;
    json["enabled"] = m_enabled;

    QJsonArray params;
    for (size_t i = 0; i < m_ctrlValues.size(); ++i) {
        QJsonObject p;
        p["index"] = static_cast<int>(i);
        p["value"] = static_cast<double>(m_ctrlValues[i]);
        params.append(p);
    }
    json["params"] = params;

    QJsonArray strParams;
    for (auto& [idx, val] : m_stringParams) {
        QJsonObject sp;
        sp["index"] = idx;
        sp["value"] = val;
        strParams.append(sp);
    }
    json["stringParams"] = strParams;

    return json;
}

void LV2Instance::stateFromJson(const QJsonObject& json) {
    if (json.contains("enabled"))
        m_enabled = json["enabled"].toBool(true);

    if (json.contains("params")) {
        QJsonArray params = json["params"].toArray();
        for (auto v : params) {
            QJsonObject p = v.toObject();
            int idx = p["index"].toInt();
            float val = static_cast<float>(p["value"].toDouble());
            if (idx >= 0 && idx < static_cast<int>(m_ctrlValues.size()))
                m_ctrlValues[idx] = val;
        }
    }

    if (json.contains("stringParams")) {
        QJsonArray strParams = json["stringParams"].toArray();
        for (auto v : strParams) {
            QJsonObject sp = v.toObject();
            int idx = sp["index"].toInt();
            QString val = sp["value"].toString();
            m_stringParams[idx] = val;
            sendPatchSet(idx, val, true);
        }
    }
}
