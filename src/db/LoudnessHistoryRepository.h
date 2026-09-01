#pragma once

namespace radio::db {

// All methods must run on the UI thread (see Database.h).
class LoudnessHistoryRepository {
public:
    // Records one EBU R128/LUFS measurement snapshot (see MixEngine's
    // momentaryLoudnessLufs()/shortTermLoudnessLufs()/
    // integratedLoudnessLufs()/outputTruePeakDb()) at the current UTC time
    // -- called periodically by AudioEngine's coarse logging timer, not per
    // audio callback (see loudness_history's migration comment).
    static void recordMeasurement(
        double integratedLufs, double momentaryLufs, double shortTermLufs, double truePeakDbfs);
};

} // namespace radio::db
