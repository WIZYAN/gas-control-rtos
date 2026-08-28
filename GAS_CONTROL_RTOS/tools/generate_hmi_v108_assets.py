"""生成V1.08串口屏阀位双色图标并清理实时页底部的旧整机模式区域。"""

from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


PROJECT_ROOT = Path(__file__).resolve().parents[1]
HMI_ROOT = PROJECT_ROOT / "hmi-screen" / "GasControl_HMI_V1.08"
IMAGE_ROOT = HMI_ROOT / "images"
FONT_PATH = HMI_ROOT / "output" / "truefont" / "wqymicrohei.ttf"


def build_valve_frame(text: str, color: tuple[int, int, int]) -> Image.Image:
    """生成55×22透明阀位文字帧，并让两个汉字在原阀位区域内居中。"""
    frame = Image.new("RGBA", (55, 22), (0, 0, 0, 0))
    draw = ImageDraw.Draw(frame)
    font = ImageFont.truetype(str(FONT_PATH), 18)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    x = (frame.width - (right - left)) // 2 - left
    y = (frame.height - (bottom - top)) // 2 - top
    draw.text((x, y), text, font=font, fill=(*color, 255))
    return frame


def write_visualtft_icon(path: Path, frames: list[Image.Image]) -> None:
    """按照VisualTFT的32位ARGB双帧格式写入ICON资源。"""
    width, height = frames[0].size
    header = struct.pack("<4sHHBB6x", b"ICON", width, height, 0x20, len(frames))
    payload = bytearray()
    for frame in frames:
        if frame.size != (width, height):
            raise ValueError("ICON中的所有帧必须尺寸一致")
        rgba = frame.tobytes()
        for offset in range(0, len(rgba), 4):
            red, green, blue, alpha = rgba[offset:offset + 4]
            payload.extend((alpha, red, green, blue))
    path.write_bytes(header + payload)


def clear_obsolete_system_mode_area(path: Path) -> None:
    """清除背景图底部旧系统模式文字和按钮，同时保留右侧菜单按钮。"""
    image = Image.open(path).convert("RGB")
    if image.size != (1024, 600):
        raise ValueError(f"实时页背景尺寸异常：{image.size}")
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 551, 807, 599), fill=(10, 24, 39))
    image.save(path)


def main() -> None:
    """生成正式ICON、预览PNG并更新普通和按下状态两张实时页背景。"""
    closed = build_valve_frame("关闭", (199, 224, 238))
    opened = build_valve_frame("开启", (53, 231, 123))
    write_visualtft_icon(IMAGE_ROOT / "Valve_State.icon", [closed, opened])

    preview = Image.new("RGB", (110, 22), (8, 18, 31))
    preview.paste(closed, (0, 0), closed)
    preview.paste(opened, (55, 0), opened)
    preview.save(IMAGE_ROOT / "Valve_State_preview.png")

    clear_obsolete_system_mode_area(IMAGE_ROOT / "Screen1.png")
    clear_obsolete_system_mode_area(IMAGE_ROOT / "Screen1_down.png")


if __name__ == "__main__":
    main()
