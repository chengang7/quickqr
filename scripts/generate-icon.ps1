# Regenerate the Windows icon and PNG preview. Windows PowerShell / System.Drawing only.
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
public static class QuickQRIcon {
 static GraphicsPath Rounded(float x,float y,float w,float h,float r) {
  var p=new GraphicsPath();float d=r*2;p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();return p;
 }
 public static Bitmap Render(int size) {
  var b=new Bitmap(size,size,System.Drawing.Imaging.PixelFormat.Format32bppArgb);
  using(var g=Graphics.FromImage(b)) {
   g.SmoothingMode=SmoothingMode.AntiAlias;g.PixelOffsetMode=PixelOffsetMode.HighQuality;g.ScaleTransform(size/256f,size/256f);
   using(var path=Rounded(8,8,240,240,60))
   using(var brush=new LinearGradientBrush(new PointF(28,18),new PointF(228,242),Color.FromArgb(139,115,255),Color.FromArgb(66,55,181))) {
    var blend=new ColorBlend();blend.Colors=new[]{Color.FromArgb(139,115,255),Color.FromArgb(102,81,232),Color.FromArgb(66,55,181)};blend.Positions=new[]{0f,.48f,1f};brush.InterpolationColors=blend;g.FillPath(brush,path);
    using(var pen=new Pen(Color.FromArgb(38,255,255,255),2))g.DrawPath(pen,path);
   }
   if(size>=32)using(var pen=new Pen(Color.FromArgb(140,255,255,255),8)) {
    pen.StartCap=pen.EndCap=LineCap.Round;pen.LineJoin=LineJoin.Round;
    g.DrawLines(pen,new[]{new PointF(44,78),new PointF(44,44),new PointF(78,44)});
    g.DrawLines(pen,new[]{new PointF(178,44),new PointF(212,44),new PointF(212,78)});
    g.DrawLines(pen,new[]{new PointF(44,178),new PointF(44,212),new PointF(78,212)});
   }
   foreach(var pt in new[]{new PointF(69,69),new PointF(147,69),new PointF(69,147)}) {
    using(var p=Rounded(pt.X,pt.Y,40,40,7))using(var pen=new Pen(Color.White,size<32?13:10))g.DrawPath(pen,p);
    if(size>=32)using(var p=Rounded(pt.X+14,pt.Y+14,12,12,2))g.FillPath(Brushes.White,p);
   }
   using(var pen=new Pen(Color.FromArgb(148,243,229),size<32?17:13)) {
    pen.StartCap=pen.EndCap=LineCap.Round;pen.LineJoin=LineJoin.Round;g.DrawLine(pen,147,193,193,147);g.DrawLines(pen,new[]{new PointF(153,147),new PointF(193,147),new PointF(193,187)});
   }
  }
  return b;
 }
}
"@
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$preview=[QuickQRIcon]::Render(512)
$preview.Save((Join-Path $OutputDirectory 'app.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()
$sizes=@(16,20,24,32,40,48,64,128,256)
$images=foreach($size in $sizes){$b=[QuickQRIcon]::Render($size);$ms=New-Object IO.MemoryStream;$b.Save($ms,[System.Drawing.Imaging.ImageFormat]::Png);,$ms.ToArray();$ms.Dispose();$b.Dispose()}
$stream=[IO.File]::Create((Join-Path $OutputDirectory 'app.ico'))
$writer=New-Object IO.BinaryWriter($stream)
$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
$offset=6+16*$sizes.Count
for($i=0;$i -lt $sizes.Count;$i++){
 $size=$sizes[$i];$dimension=if($size -eq 256){0}else{$size}
 $writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0)
 $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$images[$i].Length);$writer.Write([uint32]$offset);$offset+=$images[$i].Length
}
foreach($bytes in $images){$writer.Write([byte[]]$bytes)}
$writer.Dispose();$stream.Dispose()
Write-Output 'Generated app.svg companion assets: app.png and nine-size app.ico'
