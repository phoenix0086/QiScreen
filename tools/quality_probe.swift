// A local, synthetic moving reference for repeatable AirPlay tests. No screen capture.
import AppKit
import QuartzCore
final class PatternView: NSView {
    let start = CACurrentMediaTime()
    lazy var texture: NSImage = {
        let width = 1280, height = 720
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: width*4, bitsPerPixel: 32)!
        let pixels = bitmap.bitmapData!
        for y in 0..<height { for x in 0..<width {
            let fx=Double(x), fy=Double(y)
            let v = (sin(fx*0.031 + sin(fy*0.017)*4) + sin(fy*0.047 + fx*0.013) + sin(fx*0.18+fy*0.11)*0.2) / 2.2
            let index=(y*width+x)*4
            pixels[index]=UInt8(max(0,min(255,128+100*v)))
            pixels[index+1]=UInt8(max(0,min(255,130+95*sin(v*3+fx*0.009))))
            pixels[index+2]=UInt8(max(0,min(255,128+90*cos(v*2+fy*0.007))))
            pixels[index+3]=255
        }}
        let image=NSImage(size:NSSize(width:width,height:height));image.addRepresentation(bitmap);return image
    }()
    override var isFlipped: Bool { true }
    override func draw(_ rect: NSRect) {
        NSColor(calibratedWhite: 0.12, alpha: 1).setFill(); bounds.fill()
        func label(_ text: String, _ x: CGFloat, _ y: CGFloat, _ size: CGFloat) {
            (text as NSString).draw(at: NSPoint(x: x, y: y), withAttributes: [.font: NSFont.monospacedSystemFont(ofSize: size, weight: .medium), .foregroundColor: NSColor.white])
        }
        if CommandLine.arguments.contains("--bounds") {
            let points:[(CGFloat,CGFloat,NSColor,String)] = [
                (30,40,.magenta,"TOP LEFT"),(bounds.width-70,40,.cyan,"TOP RIGHT"),
                (30,bounds.height-80,.yellow,"BOTTOM LEFT"),(bounds.width-70,bounds.height-80,.green,"BOTTOM RIGHT")]
            NSColor.white.setStroke();let border=NSBezierPath(rect:bounds.insetBy(dx:25,dy:35));border.lineWidth=2;border.stroke()
            for (x,y,color,_) in points {color.setFill();NSRect(x:x,y:y,width:40,height:40).fill()}
            label("QiScreen: all four colored corner squares must be visible",100,120,24)
            return
        }
        label("QiScreen · motion / detail reference", 70, 60, 36)
        label("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789", 70, 130, 16)
        label("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789", 70, 175, 24)
        label("栖屏清晰度测试：细小文字、线条与移动画面", 70, 225, 28)
        for x in stride(from: 70, to: Int(bounds.width)-70, by: 4) {
            NSColor.white.setFill(); NSRect(x: x, y: 320, width: 1, height: 140).fill()
        }
        let t = CACurrentMediaTime() - start
        if CommandLine.arguments.contains("--texture") {
            NSGraphicsContext.saveGraphicsState()
            NSBezierPath(rect:NSRect(x:70,y:480,width:bounds.width-140,height:bounds.height-570)).addClip()
            texture.draw(in:NSRect(x:70+sin(t*0.7)*90,y:480,width:bounds.width-100,height:bounds.height-490))
            NSGraphicsContext.restoreGraphicsState()
        }
        let w = max(1, bounds.width - 240)
        let x = 70 + CGFloat(t.truncatingRemainder(dividingBy: 4) / 4) * w
        NSColor.systemPurple.setFill(); NSBezierPath(ovalIn: NSRect(x:x, y:520, width:100, height:100)).fill()
        for col in 0..<12 {
            NSColor(calibratedHue: CGFloat(col)/12, saturation: 0.8, brightness: 0.9, alpha: 1).setFill()
            NSRect(x: 70+CGFloat(col)*80, y: 700, width: 80, height: 100).fill()
        }
        label(String(format: "Elapsed %.2f s",t), 70, 860, 30)
    }
}
let app = NSApplication.shared
app.setActivationPolicy(.accessory)
// Never cover the user's unrelated external monitor during a receiver test.
guard let screen = NSScreen.screens.first(where: { $0.localizedName.contains("AirPlay") })
else { fatalError("Connect the AirPlay extended display first") }
let panel = NSWindow(contentRect: screen.frame, styleMask: [.borderless], backing: .buffered, defer: false)
panel.title = "QiScreen Quality Reference"; panel.isReleasedWhenClosed = false
let view = PatternView(frame: NSRect(origin: .zero, size: screen.frame.size)); panel.contentView = view
panel.setFrame(screen.frame, display: true); panel.orderFrontRegardless()
let timer = Timer.scheduledTimer(withTimeInterval: 1.0/60.0, repeats: true) { _ in view.needsDisplay = true }
print("Synthetic reference on external display: \(screen.frame.size), scale \(screen.backingScaleFactor)")
app.run()
