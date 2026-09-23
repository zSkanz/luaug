"""Author the Orbit icon family as SVG and rasterize the same geometry with Pillow.

Run from any directory: python tools/repo/draw_icons.py
Only the existing art-tool dependency (Pillow) is required.
"""

import html
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "art/editor-icons/orbit"
THEME = ROOT / "icons/default"
SCALE = 32
STROKE = 1.8


class Icon:
    """A 24-unit drawing; both exports use exactly the same geometry."""

    def __init__(self):
        self.alpha = Image.new("L", (24 * SCALE, 24 * SCALE))
        self.draw = ImageDraw.Draw(self.alpha)
        self.svg = []

    def line(self, *points, width=STROKE, closed=False, fill=False):
        coords = [(x * SCALE, y * SCALE) for x, y in points]
        color = "white"
        data = " ".join(f"{x:g},{y:g}" for x, y in points)
        tag = "polygon" if closed else "polyline"
        self.svg.append(f'<{tag} points="{data}" fill="{color if fill else "none"}" '
                        f'stroke="white" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')
        if fill:
            self.draw.polygon(coords, fill=255)
        if closed:
            coords.append(coords[0])
        self.draw.line(coords, fill=255, width=round(width * SCALE), joint="curve")
        r = width * SCALE / 2
        for x, y in coords:
            self.draw.ellipse((x-r, y-r, x+r, y+r), fill=255)
        return self

    def circle(self, x, y, r, fill=False, hole=False):
        box = tuple(v * SCALE for v in (x-r, y-r, x+r, y+r))
        if hole:
            self.draw.ellipse(box, fill=0)
        elif fill:
            self.draw.ellipse(box, fill=255)
        else:
            outer = tuple(v * SCALE for v in (x-r-STROKE/2, y-r-STROKE/2, x+r+STROKE/2, y+r+STROKE/2))
            inner = tuple(v * SCALE for v in (x-r+STROKE/2, y-r+STROKE/2, x+r-STROKE/2, y+r-STROKE/2))
            self.draw.ellipse(outer, fill=255)
            self.draw.ellipse(inner, fill=0)
        if hole:
            self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="black"/>')
        elif fill:
            self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="white"/>')
        else:
            self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r+STROKE/2}" fill="white"/>')
            self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r-STROKE/2}" fill="black"/>')
        return self

    def rect(self, x, y, w, h, r=2, fill=False):
        # Sample the corner arcs so the SVG and PNG share the same contour.
        pts = []
        for cx, cy, start in [(x+w-r,y+r,-90),(x+w-r,y+h-r,0),(x+r,y+h-r,90),(x+r,y+r,180)]:
            for step in range(9):
                a = math.radians(start + step * 90 / 8)
                pts.append((cx+r*math.cos(a), cy+r*math.sin(a)))
        return self.line(*pts, closed=True, fill=fill)

    def arc(self, x, y, r, start, end):
        return self.line(*[(x+r*math.cos(math.radians(a)), y+r*math.sin(math.radians(a)))
                           for a in [start+(end-start)*i/48 for i in range(49)]])

    def export(self, path):
        svg_path = ART / path.replace(".png", ".svg")
        svg_path.parent.mkdir(parents=True, exist_ok=True)
        # A luminance mask gives subtractive details real transparency in SVG too.
        body = "\n".join(self.svg)
        svg_path.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">\n'
                            '<defs><mask id="ink" maskUnits="userSpaceOnUse" x="0" y="0" width="24" height="24">\n'
                            + body + '\n</mask></defs><rect width="24" height="24" fill="white" mask="url(#ink)"/>\n</svg>\n', encoding="utf-8")
        image = Image.new("RGBA", (256, 256), "white")
        image.putalpha(self.alpha.resize((256, 256), Image.Resampling.LANCZOS))
        image.save(THEME / path)
        return image


def page(i):
    return i.line((6,3),(14,3),(19,8),(19,21),(6,21),closed=True).line((14,3),(14,8),(19,8))


def cube(i):
    return i.line((12,3),(21,8),(21,17),(12,22),(3,17),(3,8),closed=True).line((3,8),(12,13),(21,8)).line((12,13),(12,22))


def speaker(i):
    return i.line((3,10),(7,10),(12,6),(12,20),(7,16),(3,16),closed=True)


def text(i, y=7):
    return i.line((8,y),(16,y)).line((12,y),(12,y+9))


def arrow(i, direction):
    if direction == "left":
        i.line((15,5),(8,12),(15,19))
    elif direction == "right":
        i.line((9,5),(16,12),(9,19))
    else:
        i.line((5,15),(12,8),(19,15))
    return i


def draw_icon(group, name):
    i = Icon()
    if group == "overlay":
        i.circle(12,12,10,fill=True)
        if name == "Stamp":
            i.circle(12,12,4,hole=True)
        return i
    if group == "action":
        if name in ("Back","Forward","Up","Collapse","Expand"):
            arrow(i, {"Back":"left","Forward":"right","Up":"up","Collapse":"up","Expand":"right"}[name])
            if name == "Back": i.line((8,12),(21,12))
            if name == "Forward": i.line((3,12),(16,12))
            if name == "Collapse":
                i = Icon().line((5,9),(12,16),(19,9))
        elif name == "Add": i.line((12,4),(12,20)).line((4,12),(20,12))
        elif name == "Close": i.line((6,6),(18,18)).line((18,6),(6,18))
        elif name == "Play": i.line((8,4),(20,12),(8,20),closed=True,fill=True)
        elif name == "Pause": i.rect(6,5,3,14,0.6,True).rect(15,5,3,14,0.6,True)
        elif name == "Stop": i.rect(5,5,14,14,2,True)
        elif name == "Search": i.circle(10,10,6).line((14.5,14.5),(21,21))
        elif name in ("Locked","Unlocked"):
            i.rect(5,10,14,11).circle(12,15,1,True)
            if name == "Locked": i.arc(12,8,4,180,360).line((8,8),(8,10)).line((16,8),(16,10))
            else: i.arc(12,6,4,180,340).line((8,6),(8,10))
        elif name in ("Visible","Hidden"):
            i.line((2,12),(5,8),(9,6),(15,6),(19,8),(22,12),(19,16),(15,18),(9,18),(5,16),closed=True).circle(12,12,3)
            if name == "Hidden": i.line((3,3),(21,21),width=2.4)
        elif name in ("Undo","Redo"):
            i.line((4,9),(14,9),(18,11),(20,15),(20,19)).line((8,4),(3,9),(8,14))
            if name == "Redo":
                return mirror(i)
        elif name in ("Rotate","Refresh"):
            i.arc(12,12,8,40,310).line((13,3),(18,5),(18,2))
            if name == "Rotate": i.circle(12,12,2,True)
            else: i.line((10,21),(6,19),(6,22))
        elif name == "Move":
            i.line((12,2),(12,22)).line((2,12),(22,12))
            for pts in [((9,5),(12,2),(15,5)),((9,19),(12,22),(15,19)),((5,9),(2,12),(5,15)),((19,9),(22,12),(19,15))]: i.line(*pts)
        elif name == "Scale": i.rect(3,13,8,8,1).line((11,13),(21,3)).line((14,3),(21,3),(21,10))
        elif name == "Grid":
            for x in (4,14):
                for y in (4,14): i.rect(x,y,6,6,1)
        elif name == "Settings":
            i.circle(12,12,6).circle(12,12,2)
            for a in range(0,360,45):
                c,s = math.cos(math.radians(a)),math.sin(math.radians(a))
                i.line((12+7*c,12+7*s),(12+10*c,12+10*s),width=2.8)
        elif name == "Delete": i.line((4,6),(20,6)).line((9,3),(15,3)).line((6,6),(7,21),(17,21),(18,6)).line((10,10),(10,17)).line((14,10),(14,17))
        elif name == "Duplicate": i.rect(8,8,13,13).line((16,4),(16,3),(3,3),(3,16),(4,16))
        elif name == "New": page(i).line((9,14),(16,14)).line((12.5,10.5),(12.5,17.5))
        elif name == "Open": i.line((3,18),(3,5),(9,5),(12,8),(20,8)).line((3,20),(7,11),(22,11),(18,20),closed=True)
        elif name == "Save": i.line((4,3),(17,3),(21,7),(21,21),(4,21),closed=True).rect(8,13,9,8,0.5).line((8,3),(8,8),(16,8),(16,3))
        elif name == "Rename": i.line((4,16),(16,4),(20,8),(8,20),(3,21),closed=True).line((13,7),(17,11)).line((13,21),(21,21))
        else: raise ValueError(name)
        return i
    if group == "content":
        if name == "Other": page(i).circle(12,15,1.2,True)
        elif name == "Font": i.line((3,20),(9,4),(15,20)).line((5,14),(13,14)).line((17,11),(21,11),(21,20)).arc(19,17,2,90,360)
        elif name == "Audio": i.rect(3,3,18,18).line((7,10),(7,15)).line((12,7),(12,18)).line((17,10),(17,15))
        elif name == "Scene": i.line((3,9),(21,9),(21,21),(3,21),closed=True).line((3,9),(2,4),(20,2),(21,6),(3,9)).line((8,3),(11,7)).line((15,2),(18,6)).line((10,13),(15,16),(10,19),closed=True,fill=True)
        else: raise ValueError(name)
        return i
    if name == "Part": cube(i)
    elif name == "MeshPart": cube(i).line((3,17),(12,13),(21,17)).line((12,3),(12,13))
    elif name == "Instance": i.line((12,3),(21,12),(12,21),(3,12),closed=True).circle(12,12,2,True)
    elif name == "Folder": i.line((3,6),(9,6),(12,9),(21,9),(21,20),(3,20),closed=True)
    elif name == "Model": i.rect(8,3,8,6,1).rect(2,16,8,6,1).rect(14,16,8,6,1).line((12,9),(12,12),(6,12),(6,16)).line((12,12),(18,12),(18,16))
    elif name == "Workspace": i.line((2,16),(12,21),(22,16),(12,11),closed=True).line((5,13),(5,6),(12,2),(19,6),(19,13)).line((5,6),(12,10),(19,6)).line((12,10),(12,17))
    elif name == "Camera": i.rect(2,6,14,13).line((16,10),(22,6),(22,19),(16,15),closed=True).circle(9,12.5,3)
    elif name in ("Script","ModuleScript"):
        page(i)
        if name == "Script": i.line((9,12),(16,12)).line((9,16),(14,16))
        else: i.line((10,11),(8,14),(10,17)).line((15,11),(17,14),(15,17))
    elif name == "ScriptService": i.line((7,5),(2,12),(7,19)).line((17,5),(22,12),(17,19)).line((14,3),(10,21))
    elif name == "Frame": i.rect(3,4,18,16)
    elif name == "ScreenGui": i.rect(2,3,20,14).line((12,17),(12,21)).line((7,21),(17,21))
    elif name == "UIService": i.rect(2,3,20,18).line((2,8),(22,8)).line((9,8),(9,21))
    elif name in ("ImageLabel","ImageButton"):
        i.rect(3,4,18,16).circle(8,9,1.3,True).line((4,18),(10,12),(14,16),(18,12),(21,15))
        if name == "ImageButton": i.line((16,17),(22,20),(20,21),(19,22),closed=True,fill=True)
    elif name in ("TextLabel","TextButton","TextInput"):
        if name == "TextLabel": text(i,5).line((7,20),(17,20))
        elif name == "TextButton": i.rect(2,4,20,16); text(i,7)
        else: i.line((14,5),(3,5),(3,19),(14,19)).line((19,3),(19,21)).line((16,3),(22,3)).line((16,21),(22,21)); text(i,8)
    elif name == "ScrollFrame": i.rect(3,3,18,18).line((16,7),(16,17),width=2.6).line((7,8),(11,8)).line((7,12),(11,12)).line((7,16),(11,16))
    elif name == "UICorner": i.line((3,21),(3,11)).arc(11,11,8,180,270).line((11,3),(21,3)).line((10,21),(10,14),(14,10),(21,10))
    elif name == "UIPadding": i.rect(2,3,20,18).rect(7,8,10,8,1)
    elif name == "UIListLayout":
        for y in (5,12,19): i.circle(4,y,1,True).line((9,y),(21,y))
    elif name in ("Sound","AudioService"):
        speaker(i).arc(12,13,6,-50,50)
        if name == "AudioService": i.arc(12,13,10,-50,50)
    elif name == "AudioGroup":
        for x,y in ((5,9),(12,15),(19,7)):
            i.line((x,3),(x,y-2)).line((x,y+2),(x,21)).circle(x,y,2)
    elif name == "PointLight":
        i.circle(12,12,4)
        for a in range(0,360,45):
            c,s=math.cos(math.radians(a)),math.sin(math.radians(a))
            i.line((12+7*c,12+7*s),(12+10*c,12+10*s))
    elif name == "Lighting": i.arc(12,9,6,145,395).line((7,13),(9,17),(15,17),(17,13)).line((9,21),(15,21))
    elif name == "SpotLight": i.line((5,3),(14,7),(10,15),(2,11),closed=True).line((15,10),(22,12)).line((13,15),(19,20)).line((9,18),(10,22))
    elif name == "Material": i.circle(12,12,9).line((5,18),(18,5)).line((10,20),(20,10)).line((15,20),(20,15))
    elif name == "Attachment": i.circle(12,12,5).line((12,2),(12,7)).line((12,17),(12,22)).line((2,12),(7,12)).line((17,12),(22,12))
    elif name == "Weld": i.rect(2,7,8,10).rect(14,7,8,10).line((8,12),(16,12),width=3)
    elif name == "WeldConstraint": i.arc(8,9,5,45,310).arc(16,15,5,225,490).line((8,9),(16,15))
    elif name == "PhysicsService": i.line((4,21),(20,21)).line((6,3),(18,3)).line((12,3),(12,9)).circle(12,14,5)
    elif name in ("CharacterBody","Ragdoll"):
        i.circle(12,4,2)
        if name == "CharacterBody": i.line((4,10),(8,8),(16,8),(20,10)).line((12,8),(12,14),(7,21)).line((12,14),(17,21))
        else: i.line((4,7),(9,10),(14,9),(20,5)).line((12,10),(14,15),(9,18),(7,22)).line((14,15),(19,17),(21,21))
    elif name == "Bone": i.line((6,8),(16,18)).circle(5,5,3).circle(8,5,2).circle(19,19,3).circle(16,19,2)
    elif name == "AnimationPlayer": i.rect(2,4,20,16).line((9,8),(16,12),(9,16),closed=True,fill=True).line((6,4),(6,20))
    elif name == "TweenService": i.line((3,18),(7,18),(10,16),(14,7),(17,5),(21,5)).circle(3,18,2,True).circle(21,5,2,True)
    elif name == "RunService": i.circle(12,13,8).line((9,2),(15,2)).line((12,2),(12,5)).line((10,9),(16,13),(10,17),closed=True,fill=True)
    elif name == "HotReloadService": i.arc(12,12,9,15,290).line((11,2),(16,3),(12,6)).line((13,7),(8,13),(12,13),(11,18),(16,11),(12,11),closed=True,fill=True)
    elif name == "DebugService": i.rect(7,7,10,13,4).line((9,7),(7,3)).line((15,7),(17,3)).line((12,10),(12,17))
    elif name == "TagService": i.line((3,3),(12,3),(22,13),(13,22),(3,12),closed=True).circle(8,8,1.4,True)
    elif name == "StreamingService":
        for x,y in ((3,3),(14,3),(3,14)): i.rect(x,y,7,7,1)
        i.line((14,18),(22,18)).line((18,14),(22,18),(18,22))
    elif name == "InputService": i.rect(2,5,20,14).line((6,10),(10,10)).line((8,8),(8,12)).circle(17,10,1,True).line((8,16),(16,16))
    elif name == "InputAction": i.line((13,2),(4,13),(11,13),(10,22),(20,10),(13,10),closed=True)
    elif name == "InputBinding": i.line((3,5),(8,5),(8,19),(3,19)).line((21,5),(16,5),(16,19),(21,19)).line((5,12),(19,12)).line((12,9),(15,12),(12,15))
    elif name == "InputContext": i.rect(3,3,18,18).line((8,8),(8,17),(11,14),(14,19),(17,17),(14,13),(18,12),closed=True,fill=True)
    else: raise ValueError(name)
    if name == "DebugService":
        for y in (10,15,20): i.line((3,y),(7,y-1)).line((17,y-1),(21,y))
    return i


def mirror(i):
    i.alpha = i.alpha.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    i.svg = ['<g transform="translate(24 0) scale(-1 1)">', *i.svg, '</g>']
    return i


def main():
    theme = json.loads((THEME / "theme.json").read_text())
    paths = sorted(set(theme["icons"].values()))
    images = {}
    for path in paths:
        group, filename = path.split("/")
        images[path] = draw_icon(group, filename[:-4]).export(path)
    preview(theme, images)
    print(f"Orbit: {len(paths)} unique drawings, {len(theme['icons'])} IDs; SVG, PNG and review sheets exported.")


def preview(theme, images):
    # BOX matches the runtime atlas's coverage averaging; inspect actual UI sizes.
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 12) if Path("C:/Windows/Fonts/segoeui.ttf").exists() else ImageFont.load_default()
    for mode, bg in (("dark", "#191E28"),("light", "#F4F6FA")):
        sheet = Image.new("RGB", (1200, 80 + math.ceil(len(images)/6)*94), bg)
        d = ImageDraw.Draw(sheet)
        fg = "#DCE3F1" if mode == "dark" else "#273247"
        d.text((24,20), f"LUAUG / ORBIT    {len(images)} icons    32 / 24 / 16 / 13 px    {mode.upper()}",font=font,fill=fg)
        for n,(path,im) in enumerate(images.items()):
            x,y=24+(n%6)*198,70+(n//6)*94
            icon_id = next(k for k,v in theme["icons"].items() if v == path)
            role = theme["roles"].get(icon_id, theme["defaultRole"])
            color = theme["palette"][role][mode]
            for offset,size in ((0,32),(45,24),(82,16),(110,13)):
                alpha = im.getchannel("A").resize((size,size),Image.Resampling.BOX)
                sheet.paste(Image.new("RGB",(size,size),color),(x+offset,y+32-size),alpha)
            d.text((x,y+43),path.split("/")[1][:-4],font=font,fill=fg)
        sheet.save(ART / f"preview-{mode}.png")
    cards=[]
    for path in images:
        cards.append(f'<figure><div><img src="{path.replace(".png", ".svg")}" width="48"><img src="{path.replace(".png", ".svg")}" width="24"><img src="{path.replace(".png", ".svg")}" width="16"></div><figcaption>{html.escape(path[:-4])}</figcaption></figure>')
    (ART / "index.html").write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>LuauG Orbit icons</title>'
        '<style>body{font:14px system-ui;background:#191e28;color:#dce3f1;margin:40px}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:16px}figure{margin:0;padding:24px;background:#232a37;border-radius:12px}figure div{display:flex;gap:24px;align-items:center;height:60px}figcaption{margin-top:20px;font-size:12px}button{padding:10px;margin-bottom:24px}body.light{background:#f4f6fa;color:#273247}.light figure{background:white}.light img{filter:brightness(0.25)}</style>'
        '<h1>LuauG / Orbit</h1><p>Geometric icons with rounded strokes. 48, 24 and 16 pixels.</p>'
        '<button onclick="document.body.classList.toggle(\'light\')">Light / dark</button><main>' + ''.join(cards) + '</main></html>',encoding="utf-8")


if __name__ == "__main__":
    main()
