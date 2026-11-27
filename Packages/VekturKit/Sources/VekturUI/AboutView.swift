// AboutView.swift — About screen with the legal disclaimer.
import SwiftUI
import VekturCore

public struct AboutView: View {
    @Environment(\.dismiss) private var dismiss
    public init() {}

    public var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    BrandLogo(size: 160).frame(maxWidth: .infinity)
                    Text(ProductInfo.productName).font(.largeTitle.bold())
                    Text("\(ProductInfo.tagline) — version \(ProductInfo.marketingVersion)")
                        .foregroundStyle(.secondary)
                    Text("Created by \(ProductInfo.developer)")
                    Text("Bug reports and questions: \(ProductInfo.supportEmail)")
                    Text("Website: \(ProductInfo.website)")
                    Divider()
                    Text("Legal").font(.headline)
                    Text(ProductInfo.legalDisclaimer).font(.footnote)
                    Text(ProductInfo.copyright).font(.footnote).foregroundStyle(.secondary)
                }
                .padding()
                .textSelection(.enabled)
            }
            .navigationTitle("About")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
    }
}
