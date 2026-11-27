// ProductInfo.swift — product identity, About text and legal disclaimer.
//
// Branding rule: the reference hardware and its manufacturer are named ONLY in
// `legalDisclaimer`. Nowhere else in code, UI, presets or docs.
import Foundation

public enum ProductInfo {
    public static let productName = "Vektur Modular"
    public static let tagline = "Vintage Modular Analogue Synthesiser"
    public static let developer = "DMT Cymatics — Dream Music Temple"
    public static let supportEmail = "DMTcymatics@humanoid.net"
    public static let website = "DreamMusicTemple.com"
    public static let copyright = "© 2026 DMT Cymatics / Dream Music Temple. All rights reserved."

    public static var marketingVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.1.0"
    }

    /// Must be reviewed by counsel for each target market before commercial release.
    public static let legalDisclaimer = """
    Vektur Modular is an independently developed software instrument inspired by \
    historical modular analogue synthesis architectures. It is not manufactured by, \
    affiliated with, sponsored by, authorised by or endorsed by Roland Corporation \
    or any other original hardware manufacturer. All third-party trademarks and \
    product names belong to their respective owners and are referenced solely for \
    historical and identification purposes.
    """
}
