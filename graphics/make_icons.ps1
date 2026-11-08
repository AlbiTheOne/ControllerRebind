# Draws the small button icons of the menu (tile window, key window) for Xbox and PlayStation, clean line art like the
# controller pictures, and writes them as PNG bytes for the mod.
#   powershell -ExecutionPolicy Bypass -File make_icons.ps1 <gen folder> [preview.png]
# Out: <gen>\icons.h  (kIcon[2][16]: PNG bytes, width, height; all 96 px high)
param([string]$gen = "..\gen", [string]$preview = "")
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Collections.Generic;
using System.Text;

public static class Icons
{
    const int H = 96;
    const float LW = 5f;
    static readonly Color Line = Color.FromArgb(255, 220, 220, 220);
    static readonly Color Fill = Color.FromArgb(255, 40, 40, 40);
    static readonly Color Txt = Color.FromArgb(255, 235, 235, 235);

    static readonly string[,] Names = {
        { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "", "", "", "", "", "" },
        { "", "", "", "", "L1", "R1", "L2", "R2", "L3", "R3", "SHARE", "OPTIONS", "", "", "", "" } };
    static readonly Color[,] Face = {
        { Color.FromArgb(120, 195, 60), Color.FromArgb(225, 50, 45), Color.FromArgb(55, 140, 235), Color.FromArgb(240, 180, 30) },
        { Color.FromArgb(120, 140, 215), Color.FromArgb(220, 50, 70), Color.FromArgb(225, 120, 190), Color.FromArgb(90, 190, 120) } };

    static GraphicsPath Pill(float x, float y, float w, float h)
    {
        var p = new GraphicsPath();
        float r = h / 2;
        p.AddArc(x, y, h, h, 90, 180);
        p.AddArc(x + w - h, y, h, h, 270, 180);
        p.CloseFigure();
        return p;
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

    static void Shape(Graphics g, GraphicsPath p)
    {
        using (var b = new SolidBrush(Fill)) g.FillPath(b, p);
        using (var pen = new Pen(Line, LW) { LineJoin = LineJoin.Round }) g.DrawPath(pen, p);
    }

    static Font TextFont(float px) { return new Font("Bahnschrift SemiBold", px, FontStyle.Regular, GraphicsUnit.Pixel); }

    static float TextW(string s, float px)
    {
        using (var bmp = new Bitmap(1, 1)) using (var g = Graphics.FromImage(bmp)) using (var f = TextFont(px))
        {
            var sf = StringFormat.GenericTypographic;
            return g.MeasureString(s, f, 1000, sf).Width;
        }
    }

    // Text centred on its visible letters (cap height), not on the font's line box
    static void CenterText(Graphics g, string s, float cx, float cy, float px, Color c)
    {
        using (var f = TextFont(px))
        using (var path = new GraphicsPath(FillMode.Winding))   // Winding: the font's overlapping outlines leave no holes
        {
            path.AddString(s, f.FontFamily, (int)f.Style, px, new PointF(0, 0), StringFormat.GenericTypographic);
            var b = path.GetBounds();
            var m = new Matrix();
            m.Translate(cx - (b.X + b.Width / 2), cy - (b.Y + b.Height / 2));
            path.Transform(m);
            using (var br = new SolidBrush(c)) g.FillPath(br, path);
        }
    }

    static void PsSymbol(Graphics g, int k, float cx, float cy, float r, Color c)
    {
        using (var pen = new Pen(c, 5.5f) { LineJoin = LineJoin.Round, StartCap = LineCap.Round, EndCap = LineCap.Round })
        {
            float a = r * 0.46f;
            switch (k)
            {
                case 0: g.DrawLine(pen, cx - a, cy - a, cx + a, cy + a); g.DrawLine(pen, cx - a, cy + a, cx + a, cy - a); break;
                case 1: g.DrawEllipse(pen, cx - a * 1.08f, cy - a * 1.08f, a * 2.16f, a * 2.16f); break;
                case 2: g.DrawRectangle(pen, cx - a * 0.95f, cy - a * 0.95f, a * 1.9f, a * 1.9f); break;
                case 3: g.DrawPolygon(pen, new[] { new PointF(cx, cy - a * 1.15f), new PointF(cx + a * 1.2f, cy + a * 0.85f), new PointF(cx - a * 1.2f, cy + a * 0.85f) }); break;
            }
        }
    }

    static Bitmap New(int w, out Graphics g)
    {
        var bmp = new Bitmap(w, H, PixelFormat.Format32bppArgb);
        g = Graphics.FromImage(bmp);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        g.Clear(Color.Transparent);
        return bmp;
    }

    public static Bitmap Draw(int style, int b)
    {
        Graphics g;
        float m = LW / 2 + 2;   // room for the line
        if (b < 4)   // face buttons: a circle with the coloured letter / symbol
        {
            var bmp = New(H, out g);
            var p = new GraphicsPath(); p.AddEllipse(m, m, H - 2 * m, H - 2 * m);
            Shape(g, p);
            if (style == 0) CenterText(g, Names[0, b], H / 2f, H / 2f, 50, Face[0, b]);
            else PsSymbol(g, b, H / 2f, H / 2f, H / 2f - m, Face[1, b]);
            g.Dispose(); return bmp;
        }
        if (b >= 12)   // D-pad: a cross, the arm of this direction filled
        {
            var bmp = New(H, out g);
            float c = H / 2f, arm = H / 2f - m, w = 15;
            var p = new GraphicsPath();
            p.AddPolygon(new[] {
                new PointF(c - w, c - arm), new PointF(c + w, c - arm), new PointF(c + w, c - w), new PointF(c + arm, c - w),
                new PointF(c + arm, c + w), new PointF(c + w, c + w), new PointF(c + w, c + arm), new PointF(c - w, c + arm),
                new PointF(c - w, c + w), new PointF(c - arm, c + w), new PointF(c - arm, c - w), new PointF(c - w, c - w) });
            Shape(g, p);
            RectangleF lit;
            float inset = 9;
            switch (b)
            {
                case 12: lit = new RectangleF(c - w + inset, c - arm + inset, 2 * (w - inset), arm - w - inset); break;
                case 13: lit = new RectangleF(c - w + inset, c + w, 2 * (w - inset), arm - w - inset); break;
                case 14: lit = new RectangleF(c - arm + inset, c - w + inset, arm - w - inset, 2 * (w - inset)); break;
                default: lit = new RectangleF(c + w, c - w + inset, arm - w - inset, 2 * (w - inset)); break;
            }
            using (var br = new SolidBrush(Txt)) g.FillRectangle(br, lit);
            g.Dispose(); return bmp;
        }
        if (b == 8 || b == 9)   // stick click: a circle with a ring and its name
        {
            var bmp = New(H, out g);
            var p = new GraphicsPath(); p.AddEllipse(m, m, H - 2 * m, H - 2 * m);
            Shape(g, p);
            using (var pen = new Pen(Color.FromArgb(255, 120, 120, 120), 3f)) g.DrawEllipse(pen, 16, 16, H - 32, H - 32);
            CenterText(g, Names[style, b], H / 2f, H / 2f, 34, Txt);
            g.Dispose(); return bmp;
        }
        if (style == 0 && (b == 10 || b == 11))   // Xbox View / Menu: their symbols in a circle
        {
            var bmp = New(H, out g);
            var p = new GraphicsPath(); p.AddEllipse(m, m, H - 2 * m, H - 2 * m);
            Shape(g, p);
            using (var pen = new Pen(Txt, 4.5f) { LineJoin = LineJoin.Round, StartCap = LineCap.Round, EndCap = LineCap.Round })
            {
                if (b == 10)   // View: two overlapping windows
                {
                    g.DrawRectangle(pen, 30, 30, 26, 20);
                    using (var b2 = new SolidBrush(Fill)) g.FillRectangle(b2, 40, 42, 26, 20);
                    g.DrawRectangle(pen, 40, 42, 26, 20);
                }
                else   // Menu: three lines
                    for (int k = 0; k < 3; k++) g.DrawLine(pen, 31, 35 + k * 13, 65, 35 + k * 13);
            }
            g.Dispose(); return bmp;
        }
        // Bumpers, triggers, Share / Options: a pill with the name
        {
            string s = Names[style, b];
            float px = s.Length > 3 ? 30 : 44;
            int w = (int)Math.Ceiling(Math.Max(H * 1.3f, TextW(s, px) + 56));
            var bmp = New(w, out g);
            Shape(g, b == 6 || b == 7 ? RoundRect(m, m, w - 2 * m, H - 2 * m, 22) : Pill(m, m + 6, w - 2 * m, H - 2 * m - 12));
            CenterText(g, s, w / 2f, H / 2f, px, Txt);
            g.Dispose(); return bmp;
        }
    }

    public static string Header(string preview)
    {
        var sb = new StringBuilder();
        var table = new StringBuilder();
        var all = new List<Bitmap>();
        sb.Append("// made by graphics/make_icons.ps1 - button icons of the menu (96 px high, white lines on dark grey)\r\n");
        for (int s = 0; s < 2; s++)
        {
            table.Append("    {");
            for (int b = 0; b < 16; b++)
            {
                var bmp = Draw(s, b);
                all.Add(bmp);
                byte[] png;
                using (var ms = new System.IO.MemoryStream()) { bmp.Save(ms, ImageFormat.Png); png = ms.ToArray(); }
                sb.AppendFormat("static const unsigned char kIco_{0}_{1}[{2}] = {{", s, b, png.Length);
                for (int k = 0; k < png.Length; k++) { if (k % 32 == 0) sb.Append("\r\n"); sb.Append("0x").Append(png[k].ToString("X2")).Append(k + 1 < png.Length ? "," : ""); }
                sb.Append(" };\r\n");
                table.AppendFormat(" {{ kIco_{0}_{1}, (int)sizeof(kIco_{0}_{1}), {2}, {3} }}{4}", s, b, bmp.Width, bmp.Height, b < 15 ? "," : "");
            }
            table.Append(s == 0 ? " },\r\n" : " } };\r\n");
        }
        sb.Append("struct PadIcon { const unsigned char* png; int len; int w, h; };\r\n");
        sb.Append("static const PadIcon kIcon[2][16] = {\r\n");
        sb.Append(table.ToString());
        if (preview != "")
        {
            using (var pv = new Bitmap(1700, 2 * (H + 30) + 20, PixelFormat.Format32bppArgb))
            using (var g = Graphics.FromImage(pv))
            {
                g.Clear(Color.FromArgb(255, 24, 24, 24));
                for (int s = 0; s < 2; s++)
                {
                    float x = 15;
                    for (int b = 0; b < 16; b++) { var bm = all[s * 16 + b]; g.DrawImage(bm, x, 15 + s * (H + 30), bm.Width, bm.Height); x += bm.Width + 10; }
                }
                pv.Save(preview, ImageFormat.Png);
            }
        }
        return sb.ToString();
    }
}
'@
$genFull = (Resolve-Path $gen).Path
[IO.File]::WriteAllText((Join-Path $genFull "icons.h"), [Icons]::Header($preview))
"done"
