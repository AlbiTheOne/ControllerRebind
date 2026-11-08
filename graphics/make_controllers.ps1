# Draws the menu's two controller pictures (line art like the game's controller help: light lines, dark grey fill)
# and writes the button positions for the mod.
#   powershell -ExecutionPolicy Bypass -File make_controllers.ps1 <out folder> <gen folder>
# Out: pad_xbox.png, pad_ps.png (1000 x 700, transparent), <gen>\pad_layout.h, <gen>\pad_hl.h (button outlines)
param([string]$out = ".", [string]$gen = "..\gen", [string]$preview = "")
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

public static class PadArt
{
    const int W = 1000, H = 700;
    static readonly Color Line = Color.FromArgb(255, 205, 205, 205);
    static readonly Color Fill = Color.FromArgb(255, 44, 44, 44);
    const float LW = 4.5f;

    // Right half of an outline as Bezier points (start + 3 per segment), mirrored for the left half
    static GraphicsPath Mirrored(float[] r)
    {
        var pts = new List<PointF>();
        for (int i = 0; i < r.Length; i += 2) pts.Add(new PointF(r[i], r[i + 1]));
        var all = new List<PointF>(pts);
        for (int i = pts.Count - 2; i >= 0; i--) all.Add(new PointF(W - pts[i].X, pts[i].Y));
        var p = new GraphicsPath();
        p.AddBeziers(all.ToArray());
        p.CloseFigure();
        return p;
    }

    static GraphicsPath Bez(params float[] r)
    {
        var pts = new List<PointF>();
        for (int i = 0; i < r.Length; i += 2) pts.Add(new PointF(r[i], r[i + 1]));
        var p = new GraphicsPath();
        p.AddBeziers(pts.ToArray());
        p.CloseFigure();
        return p;
    }

    static GraphicsPath MirrorPath(GraphicsPath p)
    {
        var q = (GraphicsPath)p.Clone();
        var m = new Matrix(-1, 0, 0, 1, W, 0);
        q.Transform(m);
        return q;
    }

    static GraphicsPath RoundRect(float x, float y, float w, float h, float r)
    {
        var p = new GraphicsPath();
        p.AddArc(x, y, 2 * r, 2 * r, 180, 90);
        p.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
        p.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
        p.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
        p.CloseFigure();
        return p;
    }

    static void Shape(Graphics g, GraphicsPath p, float lw = LW)
    {
        using (var b = new SolidBrush(Fill)) g.FillPath(b, p);
        using (var pen = new Pen(Line, lw) { LineJoin = LineJoin.Round }) g.DrawPath(pen, p);
    }

    static void Circle(Graphics g, float cx, float cy, float r, float lw = LW)
    {
        var p = new GraphicsPath();
        p.AddEllipse(cx - r, cy - r, 2 * r, 2 * r);
        Shape(g, p, lw);
    }

    static void Stick(Graphics g, float cx, float cy)
    {
        Circle(g, cx, cy, 56);
        Circle(g, cx, cy, 38);
    }

    static void Dpad(Graphics g, float cx, float cy, float arm, float w)
    {
        var p = new GraphicsPath();
        float h = w / 2;
        PointF[] pts = {
            new PointF(cx - h, cy - arm), new PointF(cx + h, cy - arm), new PointF(cx + h, cy - h), new PointF(cx + arm, cy - h),
            new PointF(cx + arm, cy + h), new PointF(cx + h, cy + h), new PointF(cx + h, cy + arm), new PointF(cx - h, cy + arm),
            new PointF(cx - h, cy + h), new PointF(cx - arm, cy + h), new PointF(cx - arm, cy - h), new PointF(cx - h, cy - h) };
        p.AddPolygon(pts);
        Shape(g, p);
    }

    static void Letter(Graphics g, string s, float cx, float cy, Color c)
    {
        using (var f = new Font("Bahnschrift SemiBold", 30, FontStyle.Regular, GraphicsUnit.Pixel))
        using (var b = new SolidBrush(c))
        {
            var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
            g.DrawString(s, f, b, new RectangleF(cx - 40, cy - 38, 80, 80), sf);
        }
    }

    static void PsSymbol(Graphics g, int k, float cx, float cy, float r, Color c)
    {
        using (var pen = new Pen(c, 3.5f) { LineJoin = LineJoin.Round, StartCap = LineCap.Round, EndCap = LineCap.Round })
        {
            float a = r * 0.45f;
            switch (k)
            {
                case 0: g.DrawLine(pen, cx - a, cy - a, cx + a, cy + a); g.DrawLine(pen, cx - a, cy + a, cx + a, cy - a); break;
                case 1: g.DrawEllipse(pen, cx - a * 1.1f, cy - a * 1.1f, a * 2.2f, a * 2.2f); break;
                case 2: g.DrawRectangle(pen, cx - a, cy - a, 2 * a, 2 * a); break;
                case 3: g.DrawPolygon(pen, new[] { new PointF(cx, cy - a * 1.15f), new PointF(cx + a * 1.15f, cy + a * 0.8f), new PointF(cx - a * 1.15f, cy + a * 0.8f) }); break;
            }
        }
    }

    // Button positions (x, y, radius) in picture pixels, order A B X Y LB RB LT RT LS RS Back Start Up Down Left Right
    public static float[][] Layout = new float[2][];

    // The outline of every button: the mod draws it in blue over the picture when the button is chosen / pointed at.
    // Exclude: parts that lie over the button on the picture (a trigger behind its bumper), there no outline is drawn.
    class Hl { public GraphicsPath Path; public GraphicsPath[] Exclude; }
    static Hl[][] Hls = { new Hl[16], new Hl[16] };
    static void SetHl(int s, int i, GraphicsPath p, params GraphicsPath[] ex) { Hls[s][i] = new Hl { Path = (GraphicsPath)p.Clone(), Exclude = ex }; }
    static GraphicsPath CirclePath(float cx, float cy, float r) { var p = new GraphicsPath(); p.AddEllipse(cx - r, cy - r, 2 * r, 2 * r); return p; }
    static GraphicsPath Lines(params float[] r)
    {
        var p = new GraphicsPath();
        var pts = new List<PointF>();
        for (int i = 0; i < r.Length; i += 2) pts.Add(new PointF(r[i], r[i + 1]));
        p.AddLines(pts.ToArray());
        return p;
    }

    public static void Xbox(string file)
    {
        using (var bmp = new Bitmap(W, H, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            g.Clear(Color.Transparent);
            // Triggers (behind), then bumpers
            var lt = Bez(334, 190,  332, 112,  318, 76,  280, 72,   242, 68,  222, 100,  218, 186,  250, 188,  300, 190,  334, 190);
            Shape(g, lt); Shape(g, MirrorPath(lt));
            var lb = Bez(390, 168,  330, 138,  250, 128,  196, 164,   178, 178,  170, 196,  176, 212,  240, 178,  320, 170,  390, 186,  396, 182,  396, 172,  390, 168);
            Shape(g, lb); Shape(g, MirrorPath(lb));
            // Body
            var body = Mirrored(new float[] {
                500, 182,
                580, 182,  640, 168,  712, 168,
                790, 168,  830, 196,  848, 236,
                882, 300,  908, 396,  932, 484,
                960, 570,  950, 640,  892, 652,
                838, 662,  804, 624,  780, 584,
                754, 536,  722, 500,  662, 498,
                604, 494,  560, 506,  500, 506 });
            Shape(g, body);
            // Guide, View, Menu
            Circle(g, 500, 262, 34);
            Circle(g, 440, 330, 15);
            Circle(g, 560, 330, 15);
            Stick(g, 300, 318);
            Stick(g, 610, 432);
            Dpad(g, 392, 432, 44, 30);
            float fx = 700, fy = 318, d = 52, fr = 27;
            Circle(g, fx, fy + d, fr); Letter(g, "A", fx, fy + d, Color.FromArgb(120, 195, 60));
            Circle(g, fx + d, fy, fr); Letter(g, "B", fx + d, fy, Color.FromArgb(225, 50, 45));
            Circle(g, fx - d, fy, fr); Letter(g, "X", fx - d, fy, Color.FromArgb(55, 140, 235));
            Circle(g, fx, fy - d, fr); Letter(g, "Y", fx, fy - d, Color.FromArgb(240, 180, 30));
            bmp.Save(file, ImageFormat.Png);
            var rb = MirrorPath(lb);
            SetHl(0, 0, CirclePath(fx, fy + d, fr)); SetHl(0, 1, CirclePath(fx + d, fy, fr));
            SetHl(0, 2, CirclePath(fx - d, fy, fr)); SetHl(0, 3, CirclePath(fx, fy - d, fr));
            SetHl(0, 4, lb, body); SetHl(0, 5, rb, body);
            SetHl(0, 6, lt, lb, body); SetHl(0, 7, MirrorPath(lt), rb, body);
            SetHl(0, 8, CirclePath(300, 318, 38)); SetHl(0, 9, CirclePath(610, 432, 38));   // the stick's inner circle
            SetHl(0, 10, CirclePath(440, 330, 15)); SetHl(0, 11, CirclePath(560, 330, 15));
            float px = 392, py = 432, arm = 44, hw = 15;   // D-pad: each arm as its own closed box
            Func<float, float, float, float, GraphicsPath> box = (x0, y0, x1, y1) => { var p = new GraphicsPath(); p.AddRectangle(new RectangleF(x0, y0, x1 - x0, y1 - y0)); return p; };
            SetHl(0, 12, box(px - hw, py - arm, px + hw, py - hw));
            SetHl(0, 13, box(px - hw, py + hw, px + hw, py + arm));
            SetHl(0, 14, box(px - arm, py - hw, px - hw, py + hw));
            SetHl(0, 15, box(px + hw, py - hw, px + arm, py + hw));
            Layout[0] = new float[] {
                fx, fy + d, fr,  fx + d, fy, fr,  fx - d, fy, fr,  fx, fy - d, fr,
                262, 162, 30,  738, 162, 30,  278, 106, 32,  722, 106, 32,
                300, 318, 56,  610, 432, 56,  440, 330, 16,  560, 330, 16,
                392, 404, 15,  392, 460, 15,  364, 432, 15,  420, 432, 15 };
        }
    }

    public static void PlayStation(string file)
    {
        using (var bmp = new Bitmap(W, H, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            // L2 / R2 (behind), L1 / R1
            var l2 = Bez(330, 168,  330, 112,  312, 84,  270, 82,   228, 80,  208, 112,  212, 160,  250, 166,  292, 168,  330, 168);
            Shape(g, l2); Shape(g, MirrorPath(l2));
            var l1 = RoundRect(196, 156, 140, 42, 20);
            Shape(g, l1); Shape(g, MirrorPath(l1));
            // Body
            var body = Mirrored(new float[] {
                500, 190,
                580, 190,  630, 186,  680, 184,
                760, 180,  830, 182,  862, 232,
                898, 288,  920, 372,  946, 462,
                976, 562,  966, 642,  900, 654,
                842, 664,  806, 628,  784, 588,
                760, 546,  726, 526,  678, 526,
                622, 526,  580, 534,  500, 534 });
            Shape(g, body);
            // Touchpad, light, Share / Options, PS button
            Shape(g, RoundRect(404, 196, 192, 132, 12));
            var share = RoundRect(373, 222, 18, 52, 9);
            var options = RoundRect(609, 222, 18, 52, 9);
            Shape(g, share, 3.5f);
            Shape(g, options, 3.5f);
            Circle(g, 500, 440, 18);
            Stick(g, 396, 440);
            Stick(g, 604, 440);
            // D-pad: four separate arrow buttons in a round well
            float dx = 262, dy = 318;
            Circle(g, dx, dy, 74, 3.5f);
            Func<float, float, float, GraphicsPath> arrow = (ang, cx, cy) =>
            {
                var p = new GraphicsPath();
                p.AddPolygon(new[] { new PointF(-16, -50), new PointF(16, -50), new PointF(16, -22), new PointF(0, -8), new PointF(-16, -22) });
                var m = new Matrix(); m.Translate(cx, cy); m.Rotate(ang); p.Transform(m);
                return p;
            };
            var aUp = arrow(0, dx, dy); var aDown = arrow(180, dx, dy); var aLeft = arrow(270, dx, dy); var aRight = arrow(90, dx, dy);
            Shape(g, aUp); Shape(g, aDown); Shape(g, aLeft); Shape(g, aRight);
            // Face buttons in a round well
            float fx = 742, fy = 318, d = 52, fr = 27;
            Circle(g, fx, fy, 96, 3.5f);
            Circle(g, fx, fy + d, fr); PsSymbol(g, 0, fx, fy + d, fr, Color.FromArgb(120, 140, 215));
            Circle(g, fx + d, fy, fr); PsSymbol(g, 1, fx + d, fy, fr, Color.FromArgb(220, 50, 70));
            Circle(g, fx - d, fy, fr); PsSymbol(g, 2, fx - d, fy, fr, Color.FromArgb(225, 120, 190));
            Circle(g, fx, fy - d, fr); PsSymbol(g, 3, fx, fy - d, fr, Color.FromArgb(90, 190, 120));
            bmp.Save(file, ImageFormat.Png);
            var r1 = MirrorPath(l1);
            SetHl(1, 0, CirclePath(fx, fy + d, fr)); SetHl(1, 1, CirclePath(fx + d, fy, fr));
            SetHl(1, 2, CirclePath(fx - d, fy, fr)); SetHl(1, 3, CirclePath(fx, fy - d, fr));
            SetHl(1, 4, l1, body); SetHl(1, 5, r1, body);
            SetHl(1, 6, l2, l1, body); SetHl(1, 7, MirrorPath(l2), r1, body);
            SetHl(1, 8, CirclePath(396, 440, 38)); SetHl(1, 9, CirclePath(604, 440, 38));   // the stick's inner circle
            SetHl(1, 10, share); SetHl(1, 11, options);
            SetHl(1, 12, aUp); SetHl(1, 13, aDown); SetHl(1, 14, aLeft); SetHl(1, 15, aRight);
            Layout[1] = new float[] {
                fx, fy + d, fr,  fx + d, fy, fr,  fx - d, fy, fr,  fx, fy - d, fr,
                266, 177, 26,  734, 177, 26,  270, 116, 34,  730, 116, 34,
                396, 440, 56,  604, 440, 56,  382, 248, 18,  618, 248, 18,
                dx, dy - 34, 18,  dx, dy + 34, 18,  dx - 34, dy, 18,  dx + 34, dy, 18 };
        }
    }

    // Every outline as its own small white PNG (cut to its size), and where it goes on the picture
    public static string HlHeader()
    {
        var sb = new StringBuilder();
        var table = new StringBuilder();
        sb.Append("// made by graphics/make_controllers.ps1 - outline of every button, drawn blue over the picture by the mod\r\n");
        for (int s = 0; s < 2; s++)
        {
            table.Append("    {");
            for (int i = 0; i < 16; i++)
            {
                var hl = Hls[s][i];
                var bnd = hl.Path.GetBounds();
                int x0 = (int)Math.Floor(bnd.X - 8), y0 = (int)Math.Floor(bnd.Y - 8);
                int w = (int)Math.Ceiling(bnd.Right + 8) - x0, h = (int)Math.Ceiling(bnd.Bottom + 8) - y0;
                byte[] png;
                using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
                using (var g = Graphics.FromImage(bmp))
                {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    g.Clear(Color.Transparent);
                    g.TranslateTransform(-x0, -y0);
                    if (hl.Exclude != null) foreach (var ex in hl.Exclude) g.ExcludeClip(new Region(ex));
                    using (var pen = new Pen(Color.White, 7f) { LineJoin = LineJoin.Round, StartCap = LineCap.Round, EndCap = LineCap.Round })
                        g.DrawPath(pen, hl.Path);
                    using (var ms = new System.IO.MemoryStream()) { bmp.Save(ms, ImageFormat.Png); png = ms.ToArray(); }
                }
                sb.AppendFormat("static const unsigned char kHl_{0}_{1}[{2}] = {{", s, i, png.Length);
                for (int k = 0; k < png.Length; k++) { if (k % 32 == 0) sb.Append("\r\n"); sb.Append("0x").Append(png[k].ToString("X2")).Append(k + 1 < png.Length ? "," : ""); }
                sb.Append(" };\r\n");
                table.AppendFormat(CultureInfo.InvariantCulture, " {{ kHl_{0}_{1}, (int)sizeof(kHl_{0}_{1}), {2}.0f, {3}.0f, {4}.0f, {5}.0f }}{6}", s, i, x0, y0, w, h, i < 15 ? "," : "");
            }
            table.Append(s == 0 ? " },\r\n" : " } };\r\n");
        }
        sb.Append("struct PadHl { const unsigned char* png; int len; float x, y, w, h; };   // position and size in picture pixels\r\n");
        sb.Append("static const PadHl kPadHl[2][16] = {\r\n");
        sb.Append(table.ToString());
        return sb.ToString();
    }

    // Check picture: both controllers with every outline in blue (only for looking at, not used by the mod)
    public static void Preview(string file, string xboxPng, string psPng)
    {
        using (var bmp = new Bitmap(2 * W, H, PixelFormat.Format32bppArgb))
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.FromArgb(255, 12, 12, 12));
            string[] pics = { xboxPng, psPng };
            for (int s = 0; s < 2; s++)
            {
                using (var pic = Image.FromFile(pics[s])) g.DrawImage(pic, s * W, 0, W, H);
                for (int i = 0; i < 16; i++)
                {
                    var hl = Hls[s][i];
                    g.ResetClip();
                    g.ResetTransform();
                    g.TranslateTransform(s * W, 0);
                    if (hl.Exclude != null) foreach (var ex in hl.Exclude) g.ExcludeClip(new Region(ex));
                    using (var pen = new Pen(Color.FromArgb(255, 128, 179, 252), 7f) { LineJoin = LineJoin.Round, StartCap = LineCap.Round, EndCap = LineCap.Round })
                        g.DrawPath(pen, hl.Path);
                }
            }
            bmp.Save(file, ImageFormat.Png);
        }
    }

    public static string Header()
    {
        var sb = new StringBuilder();
        sb.Append("// made by graphics/make_controllers.ps1 - button positions on pad_xbox.png / pad_ps.png\r\n");
        sb.AppendFormat("static const float kPadPicW = {0}.f, kPadPicH = {1}.f;\r\n", W, H);
        sb.Append("// x, y, radius in picture pixels; order A B X Y LB RB LT RT LS RS Back Start Up Down Left Right\r\n");
        sb.Append("static const float kPadLayout[2][16][3] = {\r\n");
        for (int s = 0; s < 2; s++)
        {
            sb.Append("    {");
            for (int i = 0; i < 16; i++)
                sb.AppendFormat(CultureInfo.InvariantCulture, " {{ {0:0.0#}f, {1:0.0#}f, {2:0.0#}f }}{3}", Layout[s][i * 3], Layout[s][i * 3 + 1], Layout[s][i * 3 + 2], i < 15 ? "," : "");
            sb.Append(s == 0 ? " },\r\n" : " } };\r\n");
        }
        return sb.ToString();
    }
}
'@
$outFull = (Resolve-Path $out).Path
$genFull = (Resolve-Path $gen).Path
[PadArt]::Xbox((Join-Path $outFull "pad_xbox.png"))
[PadArt]::PlayStation((Join-Path $outFull "pad_ps.png"))
[IO.File]::WriteAllText((Join-Path $genFull "pad_layout.h"), [PadArt]::Header())
[IO.File]::WriteAllText((Join-Path $genFull "pad_hl.h"), [PadArt]::HlHeader())
if ($preview) { [PadArt]::Preview($preview, (Join-Path $outFull "pad_xbox.png"), (Join-Path $outFull "pad_ps.png")) }
"done"
