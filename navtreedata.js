/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "Deskflow", "index.html", [
    [ "Audio Routing — Debugging Guide", "md_docs_2dev_2audio-routing-debug.html", [
      [ "Architecture summary", "md_docs_2dev_2audio-routing-debug.html#autotoc_md1", null ],
      [ "Step 0 — Enable verbose logging on both machines", "md_docs_2dev_2audio-routing-debug.html#autotoc_md2", null ],
      [ "Step 1 — Verify the audio TCP channel while connected", "md_docs_2dev_2audio-routing-debug.html#autotoc_md3", null ],
      [ "Step 2 — Simultaneous client + server packet capture", "md_docs_2dev_2audio-routing-debug.html#autotoc_md4", null ],
      [ "Issue 1 — Windows client crash", "md_docs_2dev_2audio-routing-debug.html#autotoc_md6", [
        [ "Root cause: WASAPI format mismatch caused a buffer overread (fixed)", "md_docs_2dev_2audio-routing-debug.html#autotoc_md7", null ],
        [ "Fix applied", "md_docs_2dev_2audio-routing-debug.html#autotoc_md8", null ],
        [ "Diagnostic steps", "md_docs_2dev_2audio-routing-debug.html#autotoc_md9", null ],
        [ "Secondary suspect: COM apartment mismatch", "md_docs_2dev_2audio-routing-debug.html#autotoc_md10", null ],
        [ "Tertiary note: WASAPI loopback requires an active render stream", "md_docs_2dev_2audio-routing-debug.html#autotoc_md11", null ]
      ] ],
      [ "Issue 2 — macOS client crash after granting Screen Recording permission", "md_docs_2dev_2audio-routing-debug.html#autotoc_md13", [
        [ "Root cause: use-after-free in <span class=\"tt\">MacAudioCapture::start()</span> — file compiled without ARC (fixed)", "md_docs_2dev_2audio-routing-debug.html#autotoc_md14", null ],
        [ "Fix applied", "md_docs_2dev_2audio-routing-debug.html#autotoc_md15", null ],
        [ "Verifying the fix / diagnosing a regression", "md_docs_2dev_2audio-routing-debug.html#autotoc_md16", null ],
        [ "Secondary suspect: <span class=\"tt\">NSScreenCaptureUsageDescription</span> missing from Info.plist", "md_docs_2dev_2audio-routing-debug.html#autotoc_md17", null ],
        [ "Tertiary note: the 10-second blocking window in <span class=\"tt\">MacAudioCapture::start()</span>", "md_docs_2dev_2audio-routing-debug.html#autotoc_md18", null ]
      ] ],
      [ "Issue 3 — Wrong pitch and clipping on the server (fixed)", "md_docs_2dev_2audio-routing-debug.html#autotoc_md20", [
        [ "Symptom", "md_docs_2dev_2audio-routing-debug.html#autotoc_md21", null ],
        [ "Root cause: the Windows playback path never resampled to the device rate (fixed)", "md_docs_2dev_2audio-routing-debug.html#autotoc_md22", null ],
        [ "Fix applied", "md_docs_2dev_2audio-routing-debug.html#autotoc_md23", null ],
        [ "Verifying the fix / diagnosing a regression", "md_docs_2dev_2audio-routing-debug.html#autotoc_md24", null ]
      ] ],
      [ "Debugging from a dev build (repo binary vs release peers)", "md_docs_2dev_2audio-routing-debug.html#autotoc_md26", [
        [ "Build a debug binary", "md_docs_2dev_2audio-routing-debug.html#autotoc_md27", null ],
        [ "How the dev binary finds its settings", "md_docs_2dev_2audio-routing-debug.html#autotoc_md28", null ],
        [ "One-time setup: write the settings file via the GUI", "md_docs_2dev_2audio-routing-debug.html#autotoc_md29", null ],
        [ "Stop the release core before running the dev binary", "md_docs_2dev_2audio-routing-debug.html#autotoc_md30", null ],
        [ "Enable verbose logging for the dev run", "md_docs_2dev_2audio-routing-debug.html#autotoc_md31", null ],
        [ "Accepted CLI flags (all that exist)", "md_docs_2dev_2audio-routing-debug.html#autotoc_md32", null ]
      ] ],
      [ "Debugging from VSCode with breakpoints", "md_docs_2dev_2audio-routing-debug.html#autotoc_md34", [
        [ "Prerequisites", "md_docs_2dev_2audio-routing-debug.html#autotoc_md35", null ],
        [ "Starting a debug session", "md_docs_2dev_2audio-routing-debug.html#autotoc_md36", null ],
        [ "Binary paths per platform", "md_docs_2dev_2audio-routing-debug.html#autotoc_md37", null ],
        [ "<span class=\"tt\">--new-instance</span> is always included", "md_docs_2dev_2audio-routing-debug.html#autotoc_md38", null ],
        [ "Setting log level for the debug session", "md_docs_2dev_2audio-routing-debug.html#autotoc_md39", null ],
        [ "Useful breakpoint locations for audio routing", "md_docs_2dev_2audio-routing-debug.html#autotoc_md40", null ]
      ] ],
      [ "Combined debugging session checklist", "md_docs_2dev_2audio-routing-debug.html#autotoc_md42", [
        [ "CLI reference for deskflow-core", "md_docs_2dev_2audio-routing-debug.html#autotoc_md43", null ]
      ] ]
    ] ],
    [ "Building Deskflow", "md_docs_2dev_2build.html", [
      [ "Configuration", "md_docs_2dev_2build.html#autotoc_md45", [
        [ "Windows Configuration", "md_docs_2dev_2build.html#autotoc_md46", [
          [ "Windows and Qt", "md_docs_2dev_2build.html#autotoc_md47", [
            [ "System Qt", "md_docs_2dev_2build.html#autotoc_md48", null ],
            [ "vcpkg managed Qt", "md_docs_2dev_2build.html#autotoc_md49", null ]
          ] ]
        ] ],
        [ "macOS codesign", "md_docs_2dev_2build.html#autotoc_md50", null ]
      ] ],
      [ "Build", "md_docs_2dev_2build.html#autotoc_md51", null ],
      [ "Install", "md_docs_2dev_2build.html#autotoc_md52", null ],
      [ "Making Deskflow packages", "md_docs_2dev_2build.html#autotoc_md53", null ]
    ] ],
    [ "Contributing to Deskflow", "contributing_guide.html", [
      [ "Read the Full Guidelines", "contributing_guide.html#autotoc_md54", null ]
    ] ],
    [ "Protocol Reference", "protocol_reference.html", [
      [ "Protocol Overview", "protocol_reference.html#autotoc_md57", [
        [ "Key Implementation Files", "protocol_reference.html#autotoc_md58", null ]
      ] ],
      [ "Protocol Architecture", "protocol_reference.html#autotoc_md59", null ],
      [ "Protocol State Machine", "protocol_reference.html#autotoc_md60", [
        [ "State Descriptions", "protocol_reference.html#autotoc_md61", null ]
      ] ],
      [ "Message Categories", "protocol_reference.html#autotoc_md62", null ],
      [ "Message Reference Table", "protocol_reference.html#autotoc_md63", null ],
      [ "Typical Control Flow", "protocol_reference.html#autotoc_md64", null ],
      [ "Protocol Constraints", "protocol_reference.html#autotoc_md65", [
        [ "Message and Data Size Limits", "protocol_reference.html#autotoc_md66", null ],
        [ "TLS Handshake and Security (Protocol v1.4+)", "protocol_reference.html#autotoc_md67", null ],
        [ "Key Code and Modifier Mapping", "protocol_reference.html#autotoc_md68", null ]
      ] ],
      [ "Timing and Synchronization", "protocol_reference.html#autotoc_md69", [
        [ "Keep-Alive Mechanism (Protocol v1.3+)", "protocol_reference.html#autotoc_md70", null ],
        [ "Synchronization on Screen Entry", "protocol_reference.html#autotoc_md71", null ],
        [ "Handshake Timeout", "protocol_reference.html#autotoc_md72", null ]
      ] ],
      [ "Version Compatibility", "protocol_reference.html#autotoc_md73", [
        [ "Version Migration Guide", "protocol_reference.html#autotoc_md74", null ]
      ] ],
      [ "Implementation Examples", "protocol_reference.html#autotoc_md75", [
        [ "Connection Lifecycle", "protocol_reference.html#autotoc_md76", null ],
        [ "Message Handling", "protocol_reference.html#autotoc_md77", null ],
        [ "Complete Message Exchange Sequence", "protocol_reference.html#autotoc_md78", null ]
      ] ],
      [ "Debugging and Troubleshooting", "protocol_reference.html#autotoc_md79", [
        [ "Common Issues", "protocol_reference.html#autotoc_md80", null ],
        [ "Debug Tools", "protocol_reference.html#autotoc_md81", null ]
      ] ],
      [ "Platform-Specific Implementations", "protocol_reference.html#autotoc_md82", null ],
      [ "Implementation Checklist", "protocol_reference.html#autotoc_md83", [
        [ "Basic Client Implementation", "protocol_reference.html#autotoc_md84", null ],
        [ "Advanced Features", "protocol_reference.html#autotoc_md85", null ]
      ] ],
      [ "Reference Implementation", "protocol_reference.html#autotoc_md86", null ],
      [ "Contributing", "protocol_reference.html#autotoc_md87", null ],
      [ "Support and Resources", "protocol_reference.html#autotoc_md88", null ]
    ] ],
    [ "Deprecated List", "deprecated.html", null ],
    [ "Topics", "topics.html", "topics" ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Enumerator", "functions_eval.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", "globals_dup" ],
        [ "Functions", "globals_func.html", null ],
        [ "Variables", "globals_vars.html", "globals_vars" ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerations", "globals_enum.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"AboutDialog_8cpp.html",
"CoreIpc_8cpp.html",
"KeyTypes_8h.html#a197d8d06f6fee7887996ca35b68c0871",
"MSWindowsClipboardFacade_8cpp.html",
"OSXClipboardUTF16Converter_8h_source.html",
"ServerConfig_8cpp.html#a6150e0515f7202e2fb518f7206ed97dc",
"classAction.html#aeee2415954f8158ef9aee75fa0c7426e",
"classArchNetworkBSD.html#aeb52011a89531d24d7c27c98df555ca3",
"classClientProxy.html#a9ccd05ce4bcd4c6e9cd15efe86ff1123",
"classFingerprintDatabase.html#a96b14056d161c6b9edab1cfc6efded75",
"classIKeyState.html#abca6d885cbb75924a9e8d36b5ba0a829",
"classInputFilter_1_1MouseButtonAction.html#af7010ebdc6b82b5a31304453741c0222",
"classMSWindowsDesks.html#a8818fa1d78eb2b2925af9409e23f7ba8",
"classOSXClipboardHTMLConverter.html#ae8f8a6c576858ffd551a6725c97f900e",
"classProtocolUtil.html#a93a9ad72e30fab19fb8560431ec9522a",
"classServerConfig.html#ad3b599bd08a258a8b7cb960ce00e8c87",
"classStreamFilter.html#ac5b3ad7187d9275530e9b8ab42622629",
"classXWindowsClipboardBMPConverter.html#a26947c8699e0933e83935f23be25acae",
"classdeskflow_1_1EiScreen.html#a3dcda7daa9c790c2d396fe6e576f1290",
"classdeskflow_1_1Screen.html#a27473c23345e8173cf350aaa7f432bba",
"classdeskflow_1_1gui_1_1ipc_1_1DaemonIpcClient.html#ab2a7083cfa70433998f0db251dd69873",
"classdeskflow_1_1string_1_1CaselessCmp.html",
"globals_vars_p.html",
"namespacedeskflow_1_1core.html#a368b81c385b88a65d4ade1da45949480ac2efe4bbd13e6cb0db293e72884273c0",
"structSettings_1_1Log.html"
];

var SYNCONMSG = 'click to disable panel synchronization';
var SYNCOFFMSG = 'click to enable panel synchronization';
var LISTOFALLMEMBERS = 'List of all members';