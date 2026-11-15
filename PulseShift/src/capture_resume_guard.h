// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

// A physical button or remote command may not resume flash writes to a
// preserved, damaged or full recording. No false "recording resumed" messages.
constexpr bool canResumeCanRecording(bool initialized, bool mounted,
                                     bool preserved, bool damaged,
                                     bool storageFull, bool writeError) {
  return initialized && mounted && !preserved && !damaged &&
         !storageFull && !writeError;
}
