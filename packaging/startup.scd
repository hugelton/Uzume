// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Leo Kuroshita (@kurogedelic)
// uzume: server options for the scsynth that sclang's Crone boots (small, for a 352 MB machine).
var o = Server.default.options;
Server.program = "/opt/uzume/bin/scsynth";
o.ugenPluginsPath = ["/opt/uzume/lib/SuperCollider/plugins"];
o.memSize = 2048; o.numBuffers = 256; o.maxNodes = 512;
o.numAudioBusChannels = 128; o.numControlBusChannels = 512;
o.numInputBusChannels = 2; o.numOutputBusChannels = 2;
o.maxSynthDefs = 256; o.numRGens = 16; o.numWireBufs = 32;
o.loadDefs = false; o.blockSize = 64;
