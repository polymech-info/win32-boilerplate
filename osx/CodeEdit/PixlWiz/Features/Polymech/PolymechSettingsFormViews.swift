import AppKit
import Foundation
import SwiftUI

private enum PolymechPixelField: Hashable { case width, height }

// NSEvent local monitor: single-line AppKit TextField often does not give SwiftUI onKeyPress ↑/↓.
private final class PolymechPixelArrowNSEventRouter {
    var field: PolymechPixelField?
    var bumpW: (Int) -> Void
    var bumpH: (Int) -> Void
    private var token: Any?

    init() {
        bumpW = { _ in }
        bumpH = { _ in }
    }

    func start() {
        guard token == nil else { return }
        token = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] e in
            guard let self else { return e }
            return self.handle(e)
        }
    }

    func stop() {
        if let t = token {
            NSEvent.removeMonitor(t)
            token = nil
        }
    }

    deinit { stop() }

    private func handle(_ e: NSEvent) -> NSEvent? {
        guard let f = field else { return e }
        let step = e.modifierFlags.contains(.shift) ? 10 : 1
        switch e.keyCode {
        case 126: // up
            if f == .width { bumpW(step) } else { bumpH(step) }
            return nil
        case 125: // down
            if f == .width { bumpW(-step) } else { bumpH(-step) }
            return nil
        default:
            return e
        }
    }
}

// MARK: - Polymech form strings

private func pmFormString(_ key: String) -> String {
    NSLocalizedString(key, comment: "Polymech settings form")
}

private func resizeCropModeLabel(_ value: String) -> String {
    switch value {
    case "centre": return pmFormString("Polymech.form.cropMode.centre")
    case "attention": return pmFormString("Polymech.form.cropMode.attention")
    case "entropy": return pmFormString("Polymech.form.cropMode.entropy")
    case "low": return pmFormString("Polymech.form.cropMode.low")
    case "high": return pmFormString("Polymech.form.cropMode.high")
    default: return value
    }
}

// MARK: - Shared: sync JSON ↔ form data (Win32 CSettingsView–style)

private func polymechImageProviderNames() -> [String] {
    let providers = PolymechProviderSettingsStore.imageProviders().map(\.name)
    return providers.isEmpty ? ["google"] : providers
}

private func polymechImageModelOptions(provider: String) -> [String] {
    PolymechProviderSettingsStore.imageProvider(named: provider)?.modelOptions ?? []
}

@MainActor
private func pmReplicateApiKeyBinding() -> Binding<String> {
    Binding(
        get: { PolymechProviderSettingsStore.imageProvider(named: "replicate")?.apiKey ?? "" },
        set: { _ in }
    )
}

@MainActor
private func pmReplicateBaseURLBinding(override: Binding<String>) -> Binding<String> {
    Binding(
        get: {
            let t = override.wrappedValue.trimmingCharacters(in: .whitespacesAndNewlines)
            if !t.isEmpty { return t }
            return PolymechProviderSettingsStore.imageProvider(named: "replicate")?.baseURL
                ?? "https://api.replicate.com/v1"
        },
        set: { _ in }
    )
}

struct PolymechResizeFormView: View {
    @Binding var json: String
    @EnvironmentObject private var workflow: PolymechWorkflowState
    @State private var data: PolymechResizeFormData
    @State private var refPixels: (width: Int, height: Int)?
    @State private var pixelArrowKeyRouter = PolymechPixelArrowNSEventRouter()
    @FocusState private var focusedPixelField: PolymechPixelField?

    init(json: Binding<String>) {
        _json = json
        _data = State(initialValue: PolymechResizeFormData.from(json: json.wrappedValue))
    }

    private var firstImageURL: URL? { workflow.effectiveImageInputURLs.first }

    private var sourceAspect: Double? {
        guard let r = refPixels, r.height > 0 else { return nil }
        return Double(r.width) / Double(r.height)
    }

    /// `onChange` needs `Equatable`; `(width:height:)?` is not always usable here across toolchains.
    private var refPixelsTag: String {
        guard let p = refPixels else { return "" }
        return "\(p.width)x\(p.height)"
    }

    var body: some View {
        formContent
            .onAppear {
                syncReferencePixels()
                syncPixelArrowKeyRouter()
                pixelArrowKeyRouter.field = focusedPixelField
                if focusedPixelField != nil { pixelArrowKeyRouter.start() }
            }
            .onChange(of: json) { _, v in
                data = PolymechResizeFormData.from(json: v)
            }
            .onChange(of: data) { _, v in
                let s = v.toJSON()
                if s != json { json = s }
                syncPixelArrowKeyRouter()
            }
            .onChange(of: refPixelsTag) { _, _ in
                syncPixelArrowKeyRouter()
            }
            .onChange(of: focusedPixelField) { _, f in
                pixelArrowKeyRouter.field = f
                if f != nil {
                    pixelArrowKeyRouter.start()
                } else {
                    pixelArrowKeyRouter.stop()
                }
            }
            .onDisappear { pixelArrowKeyRouter.stop() }
            .onReceive(NotificationCenter.default.publisher(for: .polymechNavigatorContextChanged)) { _ in
                syncReferencePixels()
            }
            .onChange(of: workflow.command) { _, _ in
                syncReferencePixels()
            }
    }

    private func syncPixelArrowKeyRouter() {
        pixelArrowKeyRouter.bumpW = { bumpWidth($0) }
        pixelArrowKeyRouter.bumpH = { bumpHeight($0) }
    }

    private func syncReferencePixels() {
        if let u = firstImageURL {
            refPixels = PolymechImageURL.pixelSize(of: u)
        } else {
            refPixels = nil
        }
    }

    private var dimensionsCaption: String {
        if let p = refPixels, let u = firstImageURL {
            let name = u.lastPathComponent
            if workflow.effectiveImageInputURLs.count > 1 {
                return String(
                    format: NSLocalizedString("Polymech.resize.caption.multi", comment: "Resize form"),
                    workflow.effectiveImageInputURLs.count,
                    name,
                    p.width,
                    p.height
                )
            }
            return String(
                format: NSLocalizedString("Polymech.resize.caption.one", comment: "Resize form"),
                p.width,
                p.height,
                name
            )
        }
        return NSLocalizedString("Polymech.resize.caption.empty", comment: "Resize form")
    }

    private var footerForPixelBox: String {
        NSLocalizedString("Polymech.resize.footer.pixels", comment: "Resize form")
    }

    private var formContent: some View {
        Group {
            Section {
                Picker(pmFormString("Polymech.form.writeResultTo"), selection: $data.output_mode) {
                    Text(pmFormString("Polymech.form.outputNewFileSibling")).tag("sibling")
                    Text(pmFormString("Polymech.form.outputInPlace")).tag("in_place")
                }
                .help(
                    data.output_mode == "in_place"
                        ? NSLocalizedString("Polymech.resize.help.inPlace", comment: "Resize form")
                        : NSLocalizedString("Polymech.resize.help.sibling", comment: "Resize form")
                )
                if data.output_mode == "sibling" {
                    HStack {
                        Text(pmFormString("Polymech.form.nameInfix"))
                        Spacer()
                        TextField(pmFormString("Polymech.form.infixPlaceholderResize"), text: $data.output_basename_infix)
                            .textFieldStyle(.roundedBorder)
                            .frame(minWidth: 100)
                            .help(pmFormString("Polymech.form.infixHelpResize"))
                    }
                }
            } header: { Text(pmFormString("Polymech.form.section.output")) }

            Section {
                Text(dimensionsCaption)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Menu {
                    Button { applyOriginalSize() } label: {
                        Text(verbatim: pmFormString("Polymech.form.presetOriginal"))
                    }
                    Button { applyScale(0.5) } label: {
                        Text(verbatim: pmFormString("Polymech.form.scale50"))
                    }
                    Button { applyScale(0.75) } label: {
                        Text(verbatim: pmFormString("Polymech.form.scale75"))
                    }
                    Divider()
                    ForEach([1920, 1200, 800, 512], id: \.self) { n in
                        Button {
                            applyLongEdge(n)
                        } label: {
                            Text(verbatim: String(format: pmFormString("Polymech.form.longEdge"), n))
                        }
                    }
                } label: {
                    Label(pmFormString("Polymech.form.fitToPreset"), systemImage: "arrow.down.right.and.arrow.up.left")
                }
                HStack(alignment: .top, spacing: 10) {
                    Button {
                        var d = data
                        d.constrain_proportions.toggle()
                        if d.constrain_proportions, d.max_width > 0, d.max_height > 0, let a = sourceAspect {
                            d.max_height = max(1, Int((Double(d.max_width) / a).rounded()))
                        }
                        data = d
                    } label: {
                        Image(systemName: data.constrain_proportions ? "link.circle.fill" : "link.circle")
                            .font(.title2)
                            .symbolRenderingMode(.monochrome)
                            .foregroundStyle(
                                data.constrain_proportions
                                ? Color.accentColor
                                : Color.primary.opacity(0.5)
                            )
                    }
                    .buttonStyle(.plain)
                    .controlSize(.large)
                    .frame(minWidth: 44, minHeight: 44)
                    .contentShape(Rectangle())
                    .accessibilityLabel(pmFormString("Polymech.form.a11yConstrainProportions"))
                    .help(
                        data.constrain_proportions
                        ? pmFormString("Polymech.form.helpProportionsLinked")
                        : pmFormString("Polymech.form.helpProportionsUnlinked")
                    )
                    VStack(alignment: .leading, spacing: 8) {
                        HStack(alignment: .firstTextBaseline, spacing: 8) {
                            Text(pmFormString("Polymech.form.width"))
                                .frame(minWidth: 48, alignment: .leading)
                            Spacer(minLength: 4)
                            TextField(pmFormString("Polymech.form.placeholderDash"), text: widthStringBinding())
                                .textFieldStyle(.roundedBorder)
                                .frame(minWidth: 80, maxWidth: 120)
                                .multilineTextAlignment(.trailing)
                                .focused($focusedPixelField, equals: .width)
                            Text(pmFormString("Polymech.form.px"))
                                .foregroundStyle(.secondary)
                                .frame(width: 22, alignment: .leading)
                        }
                        HStack(alignment: .firstTextBaseline, spacing: 8) {
                            Text(pmFormString("Polymech.form.height"))
                                .frame(minWidth: 48, alignment: .leading)
                            Spacer(minLength: 4)
                            TextField(pmFormString("Polymech.form.placeholderDash"), text: heightStringBinding())
                                .textFieldStyle(.roundedBorder)
                                .frame(minWidth: 80, maxWidth: 120)
                                .multilineTextAlignment(.trailing)
                                .focused($focusedPixelField, equals: .height)
                            Text(pmFormString("Polymech.form.px"))
                                .foregroundStyle(.secondary)
                                .frame(width: 22, alignment: .leading)
                        }
                    }
                }
            } header: { Text(pmFormString("Polymech.form.section.pixelDimensions")) } footer: {
                Text(footerForPixelBox)
                    .font(.caption2)
            }

            Section {
                Picker(pmFormString("Polymech.form.fitToBox"), selection: $data.fit) {
                    Text(pmFormString("Polymech.form.fitInside")).tag("inside")
                    Text(pmFormString("Polymech.form.fitCover")).tag("cover")
                    Text(pmFormString("Polymech.form.fitContain")).tag("contain")
                    Text(pmFormString("Polymech.form.fitFill")).tag("fill")
                    Text(pmFormString("Polymech.form.fitOutside")).tag("outside")
                }
                Picker(pmFormString("Polymech.form.croppingCover"), selection: $data.position) {
                    ForEach(["centre", "attention", "entropy", "low", "high"], id: \.self) { t in
                        Text(verbatim: resizeCropModeLabel(t)).tag(t)
                    }
                }
                Picker(pmFormString("Polymech.form.interpolation"), selection: $data.kernel) {
                    Text(pmFormString("Polymech.form.kernel.lanczos3")).tag("lanczos3")
                    Text(pmFormString("Polymech.form.kernel.lanczos2")).tag("lanczos2")
                    Text(pmFormString("Polymech.form.kernel.cubic")).tag("cubic")
                    Text(pmFormString("Polymech.form.kernel.mitchell")).tag("mitchell")
                    Text(pmFormString("Polymech.form.kernel.nearest")).tag("nearest")
                }
                .help(pmFormString("Polymech.form.helpVipsKernel"))
            } header: { Text(pmFormString("Polymech.form.section.howPixelsMap")) }

            Section {
                Picker(pmFormString("Polymech.form.outputFormat"), selection: $data.format) {
                    ForEach(["jpeg", "jpg", "png", "webp", "tiff", "avif", "heic", ""], id: \.self) { t in
                        Text(verbatim: t.isEmpty ? pmFormString("Polymech.form.fromPath") : t).tag(t)
                    }
                }
                LabeledContent(pmFormString("Polymech.form.quality")) {
                    HStack {
                        Text("\(data.quality)").monospacedDigit().frame(minWidth: 24)
                        Slider(
                            value: Binding(get: { Double(data.quality) }, set: { data.quality = Int($0) }),
                            in: 1...100,
                            step: 1
                        )
                    }
                }
                Picker(pmFormString("Polymech.form.rotate"), selection: $data.rotate) {
                    Text(pmFormString("Polymech.form.rotateNone")).tag(0)
                    ForEach([90, 180, 270], id: \.self) { deg in
                        Text(verbatim: String(format: pmFormString("Polymech.form.rotateDeg"), deg)).tag(deg)
                    }
                }
                Toggle(pmFormString("Polymech.form.noEnlargement"), isOn: $data.without_enlargement)
                Toggle(pmFormString("Polymech.form.autorotateExif"), isOn: $data.autorotate)
                Toggle(pmFormString("Polymech.form.stripMetadataOnSave"), isOn: $data.strip_metadata)
                Toggle(pmFormString("Polymech.form.resultCache"), isOn: $data.cache_enabled)
            } header: { Text(pmFormString("Polymech.form.section.encoding")) }
        }
    }

    private func widthStringBinding() -> Binding<String> {
        Binding(
            get: { data.max_width > 0 ? String(data.max_width) : "" },
            set: { raw in
                let t = raw.trimmingCharacters(in: .whitespaces)
                var d = data
                let oldW = d.max_width
                let oldH = d.max_height
                if t.isEmpty {
                    d.max_width = 0
                } else {
                    let digits = String(t.filter(\.isNumber).prefix(9))
                    guard !digits.isEmpty, let v = Int(digits) else { return }
                    d.max_width = min(max(0, v), 1_000_000)
                }
                let newW = d.max_width
                if d.constrain_proportions, newW > 0, d.max_height > 0, oldW > 0, oldH > 0 {
                    if let a = sourceAspect {
                        d.max_height = max(1, Int((Double(newW) / a).rounded()))
                    } else {
                        d.max_height = max(1, Int((Double(newW) * Double(oldH) / Double(max(oldW, 1))).rounded()))
                    }
                }
                data = d
            }
        )
    }

    private func heightStringBinding() -> Binding<String> {
        Binding(
            get: { data.max_height > 0 ? String(data.max_height) : "" },
            set: { raw in
                let t = raw.trimmingCharacters(in: .whitespaces)
                var d = data
                let oldW = d.max_width
                let oldH = d.max_height
                if t.isEmpty {
                    d.max_height = 0
                } else {
                    let digits = String(t.filter(\.isNumber).prefix(9))
                    guard !digits.isEmpty, let v = Int(digits) else { return }
                    d.max_height = min(max(0, v), 1_000_000)
                }
                let newH = d.max_height
                if d.constrain_proportions, newH > 0, d.max_width > 0, oldW > 0, oldH > 0 {
                    if let a = sourceAspect {
                        d.max_width = max(1, Int((Double(newH) * a).rounded()))
                    } else {
                        d.max_width = max(1, Int((Double(newH) * Double(oldW) / Double(max(oldH, 1))).rounded()))
                    }
                }
                data = d
            }
        )
    }

    private func bumpWidth(_ delta: Int) {
        var d = data
        let oldW = d.max_width
        let oldH = d.max_height
        d.max_width = max(0, oldW + delta)
        if d.constrain_proportions, d.max_width > 0, d.max_height > 0, oldW > 0, oldH > 0 {
            if let a = sourceAspect {
                d.max_height = max(1, Int((Double(d.max_width) / a).rounded()))
            } else {
                d.max_height = max(1, Int((Double(d.max_width) * Double(oldH) / Double(max(oldW, 1))).rounded()))
            }
        }
        data = d
    }

    private func bumpHeight(_ delta: Int) {
        var d = data
        let oldW = d.max_width
        let oldH = d.max_height
        d.max_height = max(0, oldH + delta)
        if d.constrain_proportions, d.max_width > 0, d.max_height > 0, oldW > 0, oldH > 0 {
            if let a = sourceAspect {
                d.max_width = max(1, Int((Double(d.max_height) * a).rounded()))
            } else {
                d.max_width = max(1, Int((Double(d.max_height) * Double(oldW) / Double(max(oldH, 1))).rounded()))
            }
        }
        data = d
    }

    // MARK: - Presets

    private func applyOriginalSize() {
        guard let p = refPixels else { return }
        var d = data
        d.max_width = p.width
        d.max_height = p.height
        data = d
    }

    private func applyScale(_ s: Double) {
        if let p = refPixels {
            var d = data
            d.max_width = max(1, Int((Double(p.width) * s).rounded()))
            d.max_height = max(1, Int((Double(p.height) * s).rounded()))
            data = d
            return
        }
        var d = data
        if d.max_width > 0 { d.max_width = max(1, Int((Double(d.max_width) * s).rounded())) }
        if d.max_height > 0 { d.max_height = max(1, Int((Double(d.max_height) * s).rounded())) }
        data = d
    }

    private func applyLongEdge(_ L: Int) {
        if let p = refPixels, p.width > 0, p.height > 0 {
            var d = data
            if p.width >= p.height {
                d.max_width = L
                d.max_height = max(1, Int((Double(L) * Double(p.height) / Double(p.width)).rounded()))
            } else {
                d.max_height = L
                d.max_width = max(1, Int((Double(L) * Double(p.width) / Double(p.height)).rounded()))
            }
            data = d
            return
        }
        var d = data
        if d.max_width >= d.max_height, d.max_width > 0 {
            let f = Double(L) / Double(d.max_width)
            d.max_width = L
            d.max_height = max(1, Int((Double(d.max_height) * f).rounded()))
        } else if d.max_height > 0 {
            let f = Double(L) / Double(d.max_height)
            d.max_height = L
            d.max_width = max(1, Int((Double(d.max_width) * f).rounded()))
        }
        data = d
    }
}

// MARK: - Meta

struct PolymechMetaFormView: View {
    @Binding var json: String
    @State private var data: PolymechMetaFormData

    init(json: Binding<String>) {
        _json = json
        _data = State(initialValue: PolymechMetaFormData.from(json: json.wrappedValue))
    }

    var body: some View {
        formContent
            .onChange(of: json) { _, v in data = PolymechMetaFormData.from(json: v) }
            .onChange(of: data.provider) { _, provider in
                if provider == "replicate" { data.applyProviderSwitchToReplicate() }
                if provider == "google" { data.applyProviderSwitchFromReplicate() }
                if data.model.isEmpty {
                    data.model = PolymechProviderSettingsStore.imageProvider(named: provider)?.defaultModel ?? data.model
                }
            }
            .onChange(of: data) { _, v in
                let s = v.toJSON()
                if s != json { json = s }
            }
    }

    private var formContent: some View {
        Group {
            Section {
                Picker(pmFormString("Polymech.form.provider"), selection: $data.provider) {
                    ForEach(polymechImageProviderNames(), id: \.self) { p in
                        Text(p).tag(p)
                    }
                }
                if !polymechImageModelOptions(provider: data.provider).isEmpty {
                    Picker(pmFormString("Polymech.form.modelPresets"), selection: $data.model) {
                        ForEach(polymechImageModelOptions(provider: data.provider), id: \.self) { m in
                            Text(m).tag(m)
                        }
                    }
                }
                TextField(pmFormString("Polymech.form.modelId"), text: $data.model)
                if data.provider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: pmReplicateApiKeyBinding(),
                        baseURL: pmReplicateBaseURLBinding(override: $data.base_url),
                        modelSlug: $data.model,
                        modelOptions: nil,
                        command: .meta
                    )
                }
                TextField(pmFormString("Polymech.form.overridePrompt"), text: $data.prompt, axis: .vertical)
                    .lineLimit(3, reservesSpace: true)
                TextField(pmFormString("Polymech.form.userQuestion"), text: $data.user_query, axis: .vertical)
                    .lineLimit(2, reservesSpace: true)
                TextField(pmFormString("Polymech.form.apiBaseURLOptional"), text: $data.base_url)
            } header: { Text(pmFormString("Polymech.form.section.providerPrompts")) }
            Section {
                Toggle(pmFormString("Polymech.form.resizeBeforeVision"), isOn: $data.resize_first)
                Picker(pmFormString("Polymech.form.preResizeLongEdgePx"), selection: $data.resize_width) {
                    ForEach([256, 512, 768, 1024, 1536] as [Int], id: \.self) { w in
                        Text("\(w)").tag(w)
                    }
                }
            } header: { Text(pmFormString("Polymech.form.section.preflight")) }
            Section {
                Toggle(pmFormString("Polymech.form.writeStemMd"), isOn: $data.out_md)
                Toggle(pmFormString("Polymech.form.writeStemJson"), isOn: $data.out_json)
                Toggle(pmFormString("Polymech.form.updateExifInPlace"), isOn: $data.update_exif)
                TextField(pmFormString("Polymech.form.outputDir"), text: $data.out_dir)
                Toggle(pmFormString("Polymech.form.dryRun"), isOn: $data.dry_run)
            } header: { Text(pmFormString("Polymech.form.section.outputsSafety")) } footer: {
                Text(verbatim: pmFormString("Polymech.form.metaFooter"))
            }
        }
    }
}

// MARK: - Compress

struct PolymechCompressFormView: View {
    @Binding var json: String
    @State private var data: PolymechCompressFormData

    init(json: Binding<String>) {
        _json = json
        _data = State(initialValue: PolymechCompressFormData.from(json: json.wrappedValue))
    }

    var body: some View {
        formContent
            .onChange(of: json) { _, v in data = PolymechCompressFormData.from(json: v) }
            .onChange(of: data) { _, v in
                let s = v.toJSON()
                if s != json { json = s }
            }
    }

    private var formContent: some View {
        Group {
            Section {
                Picker(pmFormString("Polymech.form.writeResultTo"), selection: $data.output_mode) {
                    Text(pmFormString("Polymech.form.outputNewFileSibling")).tag("sibling")
                    Text(pmFormString("Polymech.form.outputInPlace")).tag("in_place")
                }
                .help(
                    data.output_mode == "in_place"
                        ? NSLocalizedString("Polymech.resize.help.inPlace", comment: "Resize form")
                        : NSLocalizedString("Polymech.resize.help.sibling", comment: "Resize form")
                )
                if data.output_mode == "sibling" {
                    HStack {
                        Text(pmFormString("Polymech.form.nameInfix"))
                        Spacer()
                        TextField(pmFormString("Polymech.form.infixPlaceholderCompress"), text: $data.output_basename_infix)
                            .textFieldStyle(.roundedBorder)
                            .frame(minWidth: 100)
                            .help(pmFormString("Polymech.form.infixHelpCompress"))
                    }
                }
            } header: { Text(pmFormString("Polymech.form.section.output")) }
            Section {
                Picker(pmFormString("Polymech.form.compressor"), selection: $data.compressor) {
                    Text(pmFormString("Polymech.form.compressorAuto")).tag("auto")
                    Text(pmFormString("Polymech.form.compressorMozjpeg")).tag("mozjpeg")
                    Text(pmFormString("Polymech.form.compressorPng")).tag("png")
                }
                LabeledContent(pmFormString("Polymech.form.jpegQualityMoz")) {
                    HStack {
                        Text("\(data.jpeg_quality)").monospacedDigit()
                        Slider(value: Binding(
                            get: { Double(data.jpeg_quality) },
                            set: { data.jpeg_quality = Int($0) }
                        ), in: 1...100, step: 1)
                    }
                }
                Toggle(pmFormString("Polymech.form.stripMetadata"), isOn: $data.strip_metadata)
                Toggle(pmFormString("Polymech.form.progressiveJpeg"), isOn: $data.jpeg_progressive)
                LabeledContent(pmFormString("Polymech.form.pngDeflate")) {
                    HStack {
                        Text("\(data.png_level)").monospacedDigit()
                        Slider(value: Binding(
                            get: { Double(data.png_level) },
                            set: { data.png_level = Int($0) }
                        ), in: 1...9, step: 1)
                    }
                }
                Toggle(pmFormString("Polymech.form.pngLibimagequant"), isOn: $data.png_quantize)
                Toggle(pmFormString("Polymech.form.pngZopfli"), isOn: $data.png_zopfli)
                Toggle(pmFormString("Polymech.form.mozTrellisQuant"), isOn: $data.jpeg_trellis_quant)
                Toggle(pmFormString("Polymech.form.mozOvershootDeringing"), isOn: $data.jpeg_overshoot_deringing)
            } header: { Text(pmFormString("Polymech.form.section.compression")) } footer: {
                Text(verbatim: pmFormString("Polymech.form.compressFooter"))
            }
        }
    }
}

// MARK: - Transform

struct PolymechTransformFormView: View {
    @Binding var json: String
    @State private var data: PolymechTransformFormData

    init(json: Binding<String>) {
        _json = json
        _data = State(initialValue: PolymechTransformFormData.from(json: json.wrappedValue))
    }

    var body: some View {
        formContent
            .onChange(of: json) { _, v in data = PolymechTransformFormData.from(json: v) }
            .onChange(of: data.provider) { _, provider in
                if provider == "replicate" {
                    if !data.base_url.isEmpty && !data.base_url.contains("replicate.com") { data.base_url = "" }
                } else if provider == "google" {
                    if data.base_url.contains("replicate.com") { data.base_url = "" }
                }
                if data.model.isEmpty {
                    data.model = PolymechProviderSettingsStore.imageProvider(named: provider)?.defaultModel ?? data.model
                }
            }
            .onChange(of: data) { _, v in
                let s = v.toJSON()
                if s != json { json = s }
            }
    }

    private var formContent: some View {
        Group {
            Section {
                Picker(pmFormString("Polymech.form.provider"), selection: $data.provider) {
                    ForEach(polymechImageProviderNames(), id: \.self) { p in
                        Text(p).tag(p)
                    }
                }
                if !polymechImageModelOptions(provider: data.provider).isEmpty {
                    Picker(pmFormString("Polymech.form.modelPresets"), selection: $data.model) {
                        ForEach(polymechImageModelOptions(provider: data.provider), id: \.self) { m in
                            Text(m).tag(m)
                        }
                    }
                }
                TextField(pmFormString("Polymech.form.model"), text: $data.model)
                if data.provider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: pmReplicateApiKeyBinding(),
                        baseURL: pmReplicateBaseURLBinding(override: $data.base_url),
                        modelSlug: $data.model,
                        modelOptions: nil,
                        command: .transform
                    )
                }
                TextField(pmFormString("Polymech.form.baseURLOptional"), text: $data.base_url)
            } header: { Text(pmFormString("Polymech.form.section.transformProvider")) }
            Section {
                TextField(pmFormString("Polymech.form.editGeneratePrompt"), text: $data.prompt, axis: .vertical)
                    .lineLimit(5, reservesSpace: true)
            } header: { Text(pmFormString("Polymech.form.section.transformPrompt")) }
            Section {
                Picker(pmFormString("Polymech.form.aspectGemini"), selection: $data.aspect_ratio) {
                    ForEach(["", "1:1", "4:3", "3:4", "16:9", "9:16", "21:9"], id: \.self) { t in
                        Text(verbatim: t.isEmpty ? pmFormString("Polymech.form.aspectAuto") : t).tag(t)
                    }
                }
                Picker(pmFormString("Polymech.form.imageSizeTier"), selection: $data.image_size) {
                    ForEach(["", "512", "1K", "2K", "4K"], id: \.self) { t in
                        Text(verbatim: t.isEmpty ? pmFormString("Polymech.form.sizeDefault") : t).tag(t)
                    }
                }
                Toggle(pmFormString("Polymech.form.preResizeRaster"), isOn: $data.resize_first)
                TextField(pmFormString("Polymech.form.preResizeLongEdgeZero"), value: $data.resize_width, format: .number)
                Toggle(pmFormString("Polymech.form.preResizeRawOnly"), isOn: $data.preresize_raw_only)
            } header: { Text(pmFormString("Polymech.form.section.sizeAndPreResize")) } 
        }
    }
}

// MARK: - Find

struct PolymechFindFormView: View {
    @Binding var json: String
    @State private var data: PolymechFindFormData

    init(json: Binding<String>) {
        _json = json
        _data = State(initialValue: PolymechFindFormData.from(json: json.wrappedValue))
    }

    var body: some View {
        formContent
            .onChange(of: json) { _, v in data = PolymechFindFormData.from(json: v) }
            .onChange(of: data.meta.provider) { _, provider in
                if provider == "replicate" { data.meta.applyProviderSwitchToReplicate() }
                if provider == "google" { data.meta.applyProviderSwitchFromReplicate() }
                if data.meta.model.isEmpty {
                    data.meta.model = PolymechProviderSettingsStore
                        .imageProvider(named: provider)?.defaultModel ?? data.meta.model
                }
            }
            .onChange(of: data) { _, v in
                let s = v.toJSON()
                if s != json { json = s }
            }
    }

    private var formContent: some View {
        Group {
            Section {
                Picker(pmFormString("Polymech.form.mode"), selection: $data.mode) {
                    Text(pmFormString("Polymech.form.findModeName")).tag("name")
                    Text(pmFormString("Polymech.form.findModeLlm")).tag("llm")
                }
                TextField(pmFormString("Polymech.form.searchQuery"), text: $data.prompt, axis: .vertical)
                    .lineLimit(2, reservesSpace: true)
                TextField(pmFormString("Polymech.form.judgeOverride"), text: $data.judge_prompt, axis: .vertical)
                    .lineLimit(2, reservesSpace: true)
                HStack {
                    Text(pmFormString("Polymech.form.maxResults"))
                    Spacer()
                    TextField("", value: $data.max_results, format: .number)
                        .frame(width: 56)
                }
            } header: { Text(pmFormString("Polymech.form.section.find")) }
            Section {
                Toggle(pmFormString("Polymech.form.caseInsensitiveName"), isOn: $data.case_insensitive)
                Toggle(pmFormString("Polymech.form.matchFolderNames"), isOn: $data.match_folders)
                Toggle(pmFormString("Polymech.form.recurse"), isOn: $data.recursive)
                Toggle(pmFormString("Polymech.form.dryRunShort"), isOn: $data.dry_run)
                Toggle(pmFormString("Polymech.form.llmBypassCache"), isOn: $data.bypass_cache)
                Toggle(pmFormString("Polymech.form.llmGenerateSidecars"), isOn: $data.generate)
                Toggle(pmFormString("Polymech.form.useStemMd"), isOn: $data.use_md)
                Toggle(pmFormString("Polymech.form.useStemJson"), isOn: $data.use_json)
                Toggle(pmFormString("Polymech.form.useExifCorpus"), isOn: $data.use_exif)
                Toggle(pmFormString("Polymech.form.semanticImageJudge"), isOn: $data.find_semantic_judge)
            } header: { Text(pmFormString("Polymech.form.section.nameLlm")) }
            Section {
                Picker(pmFormString("Polymech.form.catalogProvider"), selection: $data.meta.provider) {
                    ForEach(polymechImageProviderNames(), id: \.self) { p in
                        Text(p).tag(p)
                    }
                }
                if !polymechImageModelOptions(provider: data.meta.provider).isEmpty {
                    Picker(pmFormString("Polymech.form.modelPresetsNested"), selection: $data.meta.model) {
                        ForEach(polymechImageModelOptions(provider: data.meta.provider), id: \.self) { m in
                            Text(m).tag(m)
                        }
                    }
                }
                TextField(pmFormString("Polymech.form.catalogModel"), text: $data.meta.model)
                TextField(pmFormString("Polymech.form.apiBaseUrlNested"), text: $data.meta.base_url)
                if data.meta.provider == "replicate" {
                    PolymechReplicateModelBrowser(
                        apiKey: pmReplicateApiKeyBinding(),
                        baseURL: pmReplicateBaseURLBinding(override: $data.meta.base_url),
                        modelSlug: $data.meta.model,
                        modelOptions: nil,
                        command: .find
                    )
                }
                Toggle(pmFormString("Polymech.form.catalogPreresize"), isOn: $data.meta.resize_first)
                Picker(pmFormString("Polymech.form.catalogPreresizeWidth"), selection: $data.meta.resize_width) {
                    ForEach([256, 512, 768, 1024] as [Int], id: \.self) { w in
                        Text("\(w)").tag(w)
                    }
                }
            } header: { Text(pmFormString("Polymech.form.section.nestedMeta")) } footer: {
                Text(verbatim: pmFormString("Polymech.form.findFooter"))
            }
        }
    }
}
