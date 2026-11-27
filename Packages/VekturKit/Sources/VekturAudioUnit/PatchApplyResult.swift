// PatchApplyResult.swift — what happened when a patch was handed to the engine.
public struct PatchApplyResult: Sendable, Equatable {
    public struct Problem: Sendable, Equatable {
        public enum Reason: Sendable, Equatable {
            case unknownPort          // module/port key not in this build's rack
            case wrongDirection       // e.g. output → output
            case inputAlreadyPatched  // one cable per input; use a multiple to split
            case tooManyCables
            case duplicateID
            case badGain
            case notPatchable         // internal routing node or bad index

            init(engineCode: UInt8) {
                switch engineCode {
                case 2: self = .wrongDirection
                case 3: self = .inputAlreadyPatched
                case 4: self = .tooManyCables
                case 5: self = .duplicateID
                case 6: self = .badGain
                default: self = .notPatchable
                }
            }

            public var message: String {
                switch self {
                case .unknownPort: return "That jack doesn't exist in this version."
                case .wrongDirection: return "Connect an output to an input."
                case .inputAlreadyPatched: return "That input already has a cable. Use a multiple to split a signal."
                case .tooManyCables: return "The patch is full (128 cables)."
                case .duplicateID: return "Duplicate cable."
                case .badGain: return "Cable gain must be between −2 and +2."
                case .notPatchable: return "That module can't be patched."
                }
            }
        }
        public let cableID: Int
        public let reason: Reason
    }

    public var problems: [Problem] = []
    /// Cycles the engine broke with a one-block delay and a limiter (feedback patches).
    public var feedbackConnections = 0
    public var isClean: Bool { problems.isEmpty }
}
