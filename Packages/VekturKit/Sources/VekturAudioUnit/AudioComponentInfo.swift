// AudioComponentInfo.swift — the AU component identity.
// MUST match AudioComponents in AUExtension/Info.plist (checked by a unit test
// in the Xcode test plan; see docs/PHASE1_REPORT.md). Never change after release:
// hosts locate saved instances by type/subtype/manufacturer.
import AudioToolbox

public enum AudioComponentInfo {
    public static let typeCode: FourCharCode = kAudioUnitType_MusicDevice      // 'aumu'
    public static let subtypeCode: FourCharCode = fourCC("VkMd")
    public static let manufacturerCode: FourCharCode = fourCC("DMTc")            // must contain an uppercase letter
    public static let componentName = "DMT Cymatics: Vektur Modular"
    /// Encoded as Info.plist "version": major << 16 | minor << 8 | patch.
    public static let componentVersion: UInt32 = (1 << 16) | (0 << 8) | 0

    public static var description: AudioComponentDescription {
        AudioComponentDescription(componentType: typeCode, componentSubType: subtypeCode,
                                  componentManufacturer: manufacturerCode,
                                  componentFlags: 0, componentFlagsMask: 0)
    }

    public static func fourCC(_ s: String) -> FourCharCode {
        precondition(s.utf8.count == 4, "four-char code must be 4 ASCII bytes")
        return s.utf8.reduce(0) { ($0 << 8) | FourCharCode($1) }
    }
}
