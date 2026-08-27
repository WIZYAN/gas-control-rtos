from pathlib import Path
import shutil
import struct
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont


# 当前正式工程是唯一保留的大彩工程，生成脚本直接在该目录内更新可再生资源。
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_VERSION = "1.05"
PROJECT_NAME = f"GasControl_HMI_V{PROJECT_VERSION}"
PROJECT_DIR = SCRIPT_DIR / PROJECT_NAME
SOURCE_PROJECT = PROJECT_DIR

WIDTH = 1024
HEIGHT = 600
CARD_X = (12, 180, 348, 516, 684, 852)
CARD_Y = 94
CARD_WIDTH = 161
CARD_HEIGHT = 455


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    """
    函数名：load_font。
    说明：加载 Windows 微软雅黑字体，用于绘制画面背景中的中文静态标签。
    输入：size 为字号；bold 为是否使用粗体。
    输出：返回 Pillow 可使用的字体对象。
    """
    font_name = "msyhbd.ttc" if bold else "msyh.ttc"
    font_path = Path(r"C:\Windows\Fonts") / font_name
    return ImageFont.truetype(str(font_path), size=size)


FONT_12 = load_font(12)
FONT_14 = load_font(14)
FONT_16 = load_font(16)
FONT_18 = load_font(18)
FONT_20 = load_font(20)
FONT_22_BOLD = load_font(22, True)
FONT_28_BOLD = load_font(28, True)


def draw_centered(draw: ImageDraw.ImageDraw, box, text: str, font, fill) -> None:
    """
    函数名：draw_centered。
    说明：在指定矩形内水平和垂直居中绘制文字。
    输入：draw 为绘图对象；box 为矩形坐标；text 为文字；font 为字体；fill 为颜色。
    输出：无。
    """
    left, top, right, bottom = box
    text_box = draw.textbbox((0, 0), text, font=font)
    text_width = text_box[2] - text_box[0]
    text_height = text_box[3] - text_box[1]
    x = left + (right - left - text_width) / 2
    y = top + (bottom - top - text_height) / 2 - text_box[1]
    draw.text((x, y), text, font=font, fill=fill)


def draw_button(draw: ImageDraw.ImageDraw, box, label: str, fill, outline, pressed: bool = False) -> None:
    """
    函数名：draw_button。
    说明：绘制大彩按钮的静态图层，按下图层使用更明显的内阴影。
    输入：draw 为绘图对象；box 为按钮坐标；label 为按钮文字；fill 和 outline 为颜色；pressed 为按下态标志。
    输出：无。
    """
    left, top, right, bottom = box
    draw.rounded_rectangle(box, radius=8, fill=fill, outline=outline, width=2)
    if pressed:
        draw.rounded_rectangle((left + 3, top + 3, right - 3, bottom - 3), radius=6,
                               outline=(255, 255, 255), width=1)
    draw_centered(draw, box, label, FONT_16, (245, 249, 255))


def build_monitor_highlight_frames() -> tuple[Image.Image, Image.Image, Image.Image]:
    """
    函数名：build_monitor_highlight_frames。
    说明：生成气瓶卡片普通、使用高亮和低压警告红色高亮三个透明图层。
    输入：无。
    输出：依次返回普通帧、使用帧和低压警告帧，尺寸均与单张气瓶卡片一致。
    """
    normal = Image.new("RGBA", (CARD_WIDTH, CARD_HEIGHT), (0, 0, 0, 0))
    active = Image.new("RGBA", (CARD_WIDTH, CARD_HEIGHT), (0, 0, 0, 0))
    warning = Image.new("RGBA", (CARD_WIDTH, CARD_HEIGHT), (0, 0, 0, 0))

    active_draw = ImageDraw.Draw(active)
    active_draw.rounded_rectangle((1, 1, CARD_WIDTH - 2, CARD_HEIGHT - 2), radius=12,
                                  fill=(12, 111, 78, 42), outline=(52, 211, 153, 255), width=3)
    active_draw.rounded_rectangle((4, 4, CARD_WIDTH - 5, CARD_HEIGHT - 5), radius=10,
                                  outline=(52, 211, 153, 86), width=2)
    active_draw.rounded_rectangle((3, 3, CARD_WIDTH - 4, 47), radius=9,
                                  fill=(16, 119, 84, 105))
    active_draw.rectangle((3, 34, CARD_WIDTH - 4, 47), fill=(16, 119, 84, 105))
    active_draw.rounded_rectangle((31, 53, 129, 80), radius=7,
                                  fill=(20, 139, 96, 205), outline=(80, 238, 178, 255), width=2)
    active_draw.rounded_rectangle((10, 86, 150, 136), radius=8,
                                  fill=(10, 92, 70, 54), outline=(52, 211, 153, 100), width=1)

    warning_draw = ImageDraw.Draw(warning)
    warning_draw.rounded_rectangle((1, 1, CARD_WIDTH - 2, CARD_HEIGHT - 2), radius=12,
                                   fill=(126, 23, 35, 58), outline=(255, 72, 86, 255), width=3)
    warning_draw.rounded_rectangle((4, 4, CARD_WIDTH - 5, CARD_HEIGHT - 5), radius=10,
                                   outline=(255, 72, 86, 100), width=2)
    warning_draw.rounded_rectangle((3, 3, CARD_WIDTH - 4, 47), radius=9,
                                   fill=(142, 26, 39, 125))
    warning_draw.rectangle((3, 34, CARD_WIDTH - 4, 47), fill=(142, 26, 39, 125))
    warning_draw.rounded_rectangle((31, 53, 129, 80), radius=7,
                                   fill=(184, 31, 47, 220), outline=(255, 112, 122, 255), width=2)
    warning_draw.rounded_rectangle((10, 86, 150, 136), radius=8,
                                   fill=(112, 20, 31, 70), outline=(255, 72, 86, 120), width=1)

    return normal, active, warning


def save_dacai_icon(frames: tuple[Image.Image, ...], output_path: Path) -> None:
    """
    函数名：save_dacai_icon。
    说明：把同尺寸RGBA图层写成VisualTFT图标控件使用的32位ICON多帧文件。
    输入：frames为按索引排列的图标帧；output_path为目标ICON文件路径。
    输出：无；生成16字节头和逐帧ARGB像素数据组成的ICON文件。
    """
    if not frames:
        raise ValueError("图标帧不能为空")

    width, height = frames[0].size
    if (width > 0xFFFF) or (height > 0xFFFF) or (len(frames) > 0xFF):
        raise ValueError("图标尺寸或帧数超出VisualTFT ICON格式范围")
    if any(frame.size != (width, height) for frame in frames):
        raise ValueError("同一ICON文件中的所有图标帧必须尺寸一致")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack("<4sHHBB6x", b"ICON", width, height, 32, len(frames))
    with output_path.open("wb") as icon_file:
        icon_file.write(header)
        for frame in frames:
            red, green, blue, alpha = frame.convert("RGBA").split()
            icon_file.write(Image.merge("RGBA", (alpha, red, green, blue)).tobytes())


def create_monitor_highlight_icon(images_dir: Path) -> None:
    """
    函数名：create_monitor_highlight_icon。
    说明：生成实时监控页六路气瓶共用的三帧状态高亮ICON资源。
    输入：images_dir为大彩工程图片资源目录。
    输出：无；生成Gas_Cylinder_Highlight.icon文件。
    """
    save_dacai_icon(build_monitor_highlight_frames(),
                    images_dir / "Gas_Cylinder_Highlight.icon")


def create_monitor_backgrounds(images_dir: Path) -> None:
    """
    函数名：create_backgrounds。
    说明：生成 1024×600 实时监控画面背景和所有按钮的按下态裁剪图。
    输入：images_dir 为大彩工程图片目录。
    输出：无；在目标目录生成 Screen1.png 和 Screen1_down.png。
    """
    image = Image.new("RGB", (WIDTH, HEIGHT), (8, 18, 31))
    draw = ImageDraw.Draw(image)

    # 顶部信息栏仅绘制总压力和RTC背景，系统标题改由ID114文本控件显示。
    draw.rectangle((0, 0, WIDTH, 82), fill=(12, 29, 48))
    draw.rectangle((0, 79, WIDTH, 82), fill=(28, 174, 216))

    draw.rounded_rectangle((610, 10, 808, 69), radius=10, fill=(17, 43, 67), outline=(37, 91, 120), width=1)
    draw.text((625, 18), "总管压力", font=FONT_16, fill=(140, 181, 204))
    draw.text((765, 39), "MPa", font=FONT_14, fill=(140, 181, 204))

    draw.rounded_rectangle((818, 10, 1011, 69), radius=10, fill=(17, 43, 67), outline=(37, 91, 120), width=1)
    draw.text((833, 17), "系统时间（可点击校时）", font=FONT_14, fill=(140, 181, 204))

    for index, x in enumerate(CARD_X):
        # 每路采用相同的信息层级，便于工作人员横向对比六瓶状态。
        draw.rounded_rectangle((x, 94, x + 160, 548), radius=12, fill=(14, 34, 54), outline=(38, 75, 99), width=2)
        draw.rounded_rectangle((x + 2, 96, x + 158, 141), radius=10, fill=(20, 55, 82))
        draw.rectangle((x + 2, 129, x + 158, 141), fill=(20, 55, 82))
        draw.text((x + 13, 105), f"{index + 1}号气瓶", font=FONT_22_BOLD, fill=(239, 248, 255))

        draw.rounded_rectangle((x + 31, 147, x + 129, 174), radius=7, fill=(36, 50, 62),
                               outline=(88, 105, 118), width=1)
        draw.rounded_rectangle((x + 10, 180, x + 150, 230), radius=8, fill=(8, 25, 41), outline=(31, 72, 96), width=1)
        draw.text((x + 17, 184), "瓶压", font=FONT_14, fill=(112, 160, 187))
        draw.text((x + 119, 206), "MPa", font=FONT_14, fill=(112, 160, 187))

        draw.line((x + 10, 239, x + 150, 239), fill=(31, 72, 96), width=1)
        valve_rows = ((248, "进气阀"), (282, "排气阀"), (316, "测试阀"))
        for y, label in valve_rows:
            draw.text((x + 14, y), label, font=FONT_16, fill=(135, 174, 196))
            draw.rounded_rectangle((x + 91, y - 2, x + 148, y + 25), radius=6,
                                   fill=(9, 28, 46), outline=(30, 68, 91), width=1)

        # 删除重复标题和反馈提示，四个维护按钮扩大到70像素高并填满卡片剩余区域。
        draw_button(draw, (x + 9, 356, x + 77, 426), "未通过", (55, 67, 82), (103, 126, 146))
        draw_button(draw, (x + 83, 356, x + 151, 426), "人工排气", (19, 86, 132), (42, 154, 210))
        draw_button(draw, (x + 9, 438, x + 77, 508), "测试关闭", (55, 67, 82), (103, 126, 146))
        draw_button(draw, (x + 83, 438, x + 151, 508), "正常使用", (29, 91, 90), (51, 157, 143))

    draw.rectangle((0, 558, WIDTH, HEIGHT), fill=(10, 24, 39))
    draw.text((18, 568), "系统模式", font=FONT_14, fill=(142, 181, 204))
    draw_button(draw, (180, 564, 392, 594), "自动运行", (22, 78, 64), (52, 211, 153))
    draw.text((410, 568), "切换到安全停止将立即关闭全部十八路阀门", font=FONT_14, fill=(163, 190, 207))
    draw_button(draw, (818, 564, 908, 594), "主菜单", (39, 63, 82), (89, 125, 148))
    draw_button(draw, (916, 564, 1014, 594), "日志查询", (19, 86, 132), (42, 154, 210))

    images_dir.mkdir(parents=True, exist_ok=True)
    image.save(images_dir / "Screen1.png")

    # 按下态图与主背景坐标完全一致，由大彩按钮控件按坐标裁剪对应区域。
    down_image = image.copy()
    down_draw = ImageDraw.Draw(down_image)
    for x in CARD_X:
        draw_button(down_draw, (x + 9, 356, x + 77, 426), "已通过", (19, 132, 92), (52, 211, 153), True)
        draw_button(down_draw, (x + 83, 356, x + 151, 426), "排气中", (180, 112, 22), (248, 183, 66), True)
        draw_button(down_draw, (x + 9, 438, x + 77, 508), "测试开启", (19, 113, 159), (53, 202, 240), True)
        draw_button(down_draw, (x + 83, 438, x + 151, 508), "已停用", (153, 50, 63), (247, 98, 111), True)
    draw_button(down_draw, (180, 564, 392, 594), "安全停止", (121, 48, 60), (255, 112, 124), True)
    draw_button(down_draw, (818, 564, 908, 594), "主菜单", (24, 109, 151), (69, 214, 255), True)
    draw_button(down_draw, (916, 564, 1014, 594), "日志查询", (20, 126, 164), (69, 214, 255), True)
    down_image.save(images_dir / "Screen1_down.png")


def draw_menu_icon(draw: ImageDraw.ImageDraw, center_x: int, center_y: int,
                   icon_kind: str, accent_color: tuple[int, int, int]) -> None:
    """
    函数名：draw_menu_icon。
    说明：在主菜单圆形区域绘制实时监控、日志查询或参数设置线性图标。
    输入：draw为画布；center_x和center_y为图标中心；icon_kind为图标类型；accent_color为强调色。
    输出：无；图标直接绘制到传入画布。
    """
    light_color = (230, 246, 255)

    if icon_kind == "monitor":
        draw.arc((center_x - 43, center_y - 36, center_x + 43, center_y + 50),
                 198, 342, fill=light_color, width=5)
        draw.line((center_x - 34, center_y + 22, center_x - 39, center_y + 14),
                  fill=light_color, width=4)
        draw.line((center_x, center_y - 17, center_x, center_y - 27),
                  fill=light_color, width=4)
        draw.line((center_x + 34, center_y + 22, center_x + 39, center_y + 14),
                  fill=light_color, width=4)
        draw.line((center_x, center_y + 19, center_x + 24, center_y - 5),
                  fill=accent_color, width=6)
        draw.ellipse((center_x - 7, center_y + 12, center_x + 7, center_y + 26),
                     fill=light_color)
        draw.line((center_x - 28, center_y + 38, center_x + 28, center_y + 38),
                  fill=light_color, width=4)
    elif icon_kind == "log":
        draw.rounded_rectangle((center_x - 35, center_y - 42,
                                center_x + 26, center_y + 40),
                               radius=6, outline=light_color, width=4)
        draw.line((center_x - 22, center_y - 20, center_x + 13, center_y - 20),
                  fill=light_color, width=4)
        draw.line((center_x - 22, center_y - 2, center_x + 8, center_y - 2),
                  fill=light_color, width=4)
        draw.line((center_x - 22, center_y + 16, center_x - 2, center_y + 16),
                  fill=light_color, width=4)
        draw.ellipse((center_x + 1, center_y + 3, center_x + 43, center_y + 45),
                     fill=(27, 86, 92), outline=accent_color, width=4)
        draw.line((center_x + 22, center_y + 12, center_x + 22, center_y + 25),
                  fill=light_color, width=4)
        draw.line((center_x + 22, center_y + 25, center_x + 32, center_y + 30),
                  fill=light_color, width=4)
    elif icon_kind == "config":
        slider_y = (center_y - 25, center_y, center_y + 25)
        slider_x = (center_x - 37, center_x + 37)
        knob_x = (center_x - 14, center_x + 17, center_x - 3)
        for y, knob in zip(slider_y, knob_x):
            draw.line((slider_x[0], y, slider_x[1], y), fill=light_color, width=4)
            draw.ellipse((knob - 7, y - 7, knob + 7, y + 7),
                         fill=accent_color, outline=light_color, width=3)
        draw.arc((center_x + 17, center_y + 12, center_x + 43, center_y + 40),
                 180, 360, fill=light_color, width=3)
        draw.rounded_rectangle((center_x + 15, center_y + 25,
                                center_x + 45, center_y + 47),
                               radius=4, fill=(85, 59, 100), outline=light_color, width=3)


def create_menu_backgrounds(images_dir: Path) -> None:
    """
    函数名：create_menu_backgrounds。
    说明：生成系统启动主菜单的正常态和按钮按下态背景。
    输入：images_dir 为大彩工程图片目录。
    输出：无；生成 Screen0.png 和 Screen0_down.png。
    """
    image = Image.new("RGB", (WIDTH, HEIGHT), (8, 18, 31))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, WIDTH, 88), fill=(12, 29, 48))
    draw.rectangle((0, 85, WIDTH, 88), fill=(28, 174, 216))

    menu_cards = ((32, 154, 316, 444), (370, 154, 654, 444), (708, 154, 992, 444))
    card_colors = (((14, 38, 60), (38, 98, 131)),
                   ((14, 42, 54), (38, 105, 106)),
                   ((43, 35, 55), (119, 85, 139)))
    for box, colors in zip(menu_cards, card_colors):
        draw.rounded_rectangle(box, radius=18, fill=colors[0], outline=colors[1], width=2)
    draw.ellipse((112, 196, 236, 320), fill=(19, 95, 135), outline=(69, 214, 255), width=3)
    draw.ellipse((450, 196, 574, 320), fill=(27, 86, 92), outline=(52, 211, 153), width=3)
    draw.ellipse((788, 196, 912, 320), fill=(85, 59, 100), outline=(198, 145, 231), width=3)
    draw_menu_icon(draw, 174, 258, "monitor", (69, 214, 255))
    draw_menu_icon(draw, 512, 258, "log", (52, 211, 153))
    draw_menu_icon(draw, 850, 258, "config", (198, 145, 231))
    draw_centered(draw, (32, 348, 316, 404), "实时监控", FONT_28_BOLD, (239, 248, 255))
    draw_centered(draw, (370, 348, 654, 404), "日志查询", FONT_28_BOLD, (239, 248, 255))
    draw_centered(draw, (708, 348, 992, 404), "参数设置", FONT_28_BOLD, (239, 248, 255))
    image.save(images_dir / "Screen0.png")

    down_image = image.copy()
    down_draw = ImageDraw.Draw(down_image)
    down_draw.rounded_rectangle(menu_cards[0], radius=18, fill=(19, 83, 116), outline=(69, 214, 255), width=3)
    down_draw.rounded_rectangle(menu_cards[1], radius=18, fill=(23, 93, 91), outline=(52, 211, 153), width=3)
    down_draw.rounded_rectangle(menu_cards[2], radius=18, fill=(94, 61, 111), outline=(221, 166, 248), width=3)
    down_draw.ellipse((112, 196, 236, 320), fill=(19, 95, 135), outline=(69, 214, 255), width=4)
    down_draw.ellipse((450, 196, 574, 320), fill=(27, 86, 92), outline=(52, 211, 153), width=4)
    down_draw.ellipse((788, 196, 912, 320), fill=(85, 59, 100), outline=(221, 166, 248), width=4)
    draw_menu_icon(down_draw, 174, 258, "monitor", (69, 214, 255))
    draw_menu_icon(down_draw, 512, 258, "log", (52, 211, 153))
    draw_menu_icon(down_draw, 850, 258, "config", (221, 166, 248))
    draw_centered(down_draw, (32, 348, 316, 404), "实时监控", FONT_28_BOLD, (255, 255, 255))
    draw_centered(down_draw, (370, 348, 654, 404), "日志查询", FONT_28_BOLD, (255, 255, 255))
    draw_centered(down_draw, (708, 348, 992, 404), "参数设置", FONT_28_BOLD, (255, 255, 255))
    down_image.save(images_dir / "Screen0_down.png")


def create_menu_preview(project_dir: Path) -> None:
    """
    函数名：create_menu_preview。
    说明：把115号标题文本的默认效果叠加到主菜单背景，生成便于核对布局的静态预览图。
    输入：project_dir为大彩画面工程目录。
    输出：无；在工程目录生成“主菜单实际预览.png”。
    """
    with Image.open(project_dir / "images" / "Screen0.png") as source_image:
        preview = source_image.copy()
    preview_draw = ImageDraw.Draw(preview)
    draw_centered(preview_draw, (212, 20, 812, 62),
                  "六瓶三阀气源控制系统", FONT_28_BOLD, (239, 248, 255))
    preview.save(project_dir / "主菜单实际预览.png")


def create_config_backgrounds(images_dir: Path) -> None:
    """
    函数名：create_config_backgrounds。
    说明：生成密码保护参数设置画面的正常态和按钮按下态背景。
    输入：images_dir为大彩工程图片目录。
    输出：无；生成Screen4.png和Screen4_down.png。
    """
    image = Image.new("RGB", (WIDTH, HEIGHT), (8, 18, 31))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, WIDTH, 88), fill=(12, 29, 48))
    draw.rectangle((0, 85, WIDTH, 88), fill=(28, 174, 216))

    left_fields = (
        ("切瓶压力", "MPa", "大于0"), ("待用最低压力", "MPa", "不低于切瓶压力+0.1MPa"),
        ("低压警告压力", "MPa", "1.5～5.0"), ("压力合法上限", "MPa", "不低于警告值"),
        ("12V强吸合时间", "ms", "1～65535"), ("低压确认时间", "ms", "0～65535"))
    right_fields = (
        ("低压确认样本数", "次", "1～255"), ("关闭阀等待", "ms", "0～65535"),
        ("打开阀等待", "ms", "0～65535"), ("手动排气时间", "s", "3.000～65.535"),
        ("测试阀最长开启", "s", "5～60"))

    def draw_field(column_x: int, y: int, label: str, unit: str, hint: str) -> None:
        draw.rounded_rectangle((column_x, y, column_x + 464, y + 48), radius=7,
                               fill=(13, 35, 55), outline=(36, 73, 98), width=1)
        draw.text((column_x + 14, y + 7), label, font=FONT_16, fill=(223, 240, 249))
        hint_font = FONT_12 if label == "待用最低压力" else FONT_14
        hint_x = column_x + 142 if label == "待用最低压力" else column_x + 168
        draw.text((hint_x, y + 26), hint, font=hint_font, fill=(124, 170, 196))
        draw.rounded_rectangle((column_x + 280, y + 6, column_x + 410, y + 42), radius=5,
                               fill=(7, 24, 39), outline=(47, 135, 173), width=2)
        draw.text((column_x + 418, y + 14), unit, font=FONT_14, fill=(163, 190, 207))

    for index, field in enumerate(left_fields):
        draw_field(24, 100 + index * 54, *field)
    for index, field in enumerate(right_fields):
        draw_field(536, 100 + index * 67, *field)
    # 删除紫色内部实现提示，右侧五项均匀铺满与左侧相同高度的参数区域。
    draw.rounded_rectangle((24, 474, 1000, 528), radius=7, fill=(10, 29, 46),
                           outline=(47, 135, 173), width=2)
    draw.text((38, 489), "操作结果", font=FONT_16, fill=(163, 190, 207))

    buttons = (
        (298, 540, 498, 590, "恢复默认", (24, 83, 112), (69, 214, 255)),
        (526, 540, 726, 590, "返回主菜单", (24, 83, 112), (69, 214, 255)))
    for left, top, right, bottom, label, fill, outline in buttons:
        draw_button(draw, (left, top, right, bottom), label, fill, outline)
    image.save(images_dir / "Screen4.png")

    down = image.copy()
    down_draw = ImageDraw.Draw(down)
    for left, top, right, bottom, label, fill, outline in buttons:
        draw_button(down_draw, (left, top, right, bottom), label,
                    tuple(min(channel + 20, 255) for channel in fill),
                    tuple(min(channel + 20, 255) for channel in outline), True)
    down.save(images_dir / "Screen4_down.png")


def create_config_dialog_backgrounds(images_dir: Path) -> None:
    """
    函数名：create_config_dialog_backgrounds。
    说明：生成参数逐项确认画面的独立背景和两个按钮的按下态背景。
    输入：images_dir为大彩工程图片目录。
    输出：无；生成Screen5.png和Screen5_down.png。
    """
    image = Image.new("RGBA", (WIDTH, HEIGHT), (8, 18, 31, 255))
    draw = ImageDraw.Draw(image)
    dialog = (224, 132, 800, 476)
    draw.rounded_rectangle(dialog, radius=18, fill=(10, 29, 46, 255),
                           outline=(69, 214, 255, 255), width=3)
    draw.rectangle((227, 135, 797, 205), fill=(15, 43, 68, 255))
    draw_centered(draw, (224, 148, 800, 190), "确认修改参数", FONT_28_BOLD, (239, 248, 255, 255))
    draw.text((278, 266), "当前值", font=FONT_16, fill=(163, 190, 207, 255))
    draw.text((608, 266), "新值", font=FONT_16, fill=(163, 190, 207, 255))
    draw.rounded_rectangle((270, 294, 442, 338), radius=7, fill=(7, 24, 39, 255),
                           outline=(47, 135, 173, 255), width=2)
    draw_centered(draw, (466, 294, 558, 338), "→", FONT_28_BOLD, (69, 214, 255, 255))
    draw.rounded_rectangle((582, 294, 754, 338), radius=7, fill=(7, 24, 39, 255),
                           outline=(52, 211, 153, 255), width=2)
    # 底部说明由Screen5的ID113动态文本控件显示，背景图不再绘制固定文字。
    buttons = (
        (278, 399, 498, 459, "返回修改", (45, 60, 76, 255), (124, 170, 196, 255)),
        (526, 399, 746, 459, "确认并应用", (22, 78, 64, 255), (52, 211, 153, 255)))
    for left, top, right, bottom, _label, fill, outline in buttons:
        draw_button(draw, (left, top, right, bottom), "", fill, outline)
    image.save(images_dir / "Screen5.png")

    down = image.copy()
    down_draw = ImageDraw.Draw(down)
    for left, top, right, bottom, _label, fill, outline in buttons:
        pressed_fill = tuple(min(channel + 20, 255) for channel in fill[:3]) + (255,)
        pressed_outline = tuple(min(channel + 20, 255) for channel in outline[:3]) + (255,)
        draw_button(down_draw, (left, top, right, bottom), "",
                    pressed_fill, pressed_outline, True)
    down.save(images_dir / "Screen5_down.png")


def create_log_backgrounds(images_dir: Path) -> None:
    """
    函数名：create_log_backgrounds。
    说明：生成事件日志和常规日志两个查询画面的正常态及按钮按下态背景。
    输入：images_dir 为大彩工程图片目录。
    输出：无；生成 Screen2、Screen3 的正常态和按下态背景图片。
    """
    event_image = Image.new("RGB", (WIDTH, HEIGHT), (8, 18, 31))
    event_draw = ImageDraw.Draw(event_image)
    event_draw.rectangle((0, 0, WIDTH, 90), fill=(12, 29, 48))
    event_draw.rectangle((0, 87, WIDTH, 90), fill=(28, 174, 216))
    draw_button(event_draw, (210, 17, 365, 67), "事件日志", (19, 86, 132), (42, 154, 210))
    draw_button(event_draw, (375, 17, 530, 67), "常规日志", (39, 63, 82), (89, 125, 148))
    draw_button(event_draw, (676, 17, 835, 67), "刷新事件", (19, 86, 132), (42, 154, 210))
    draw_button(event_draw, (846, 17, 1008, 67), "主菜单", (39, 63, 82), (89, 125, 148))

    event_draw.rounded_rectangle((16, 100, 1008, 560), radius=10, fill=(10, 27, 44),
                                 outline=(38, 75, 99), width=2)
    event_draw.rectangle((18, 102, 1006, 134), fill=(20, 55, 82))
    event_column_x = (18, 255, 354, 769, 1006)
    for index, text in enumerate(("时间", "气瓶", "状态变化", "压力")):
        draw_centered(event_draw, (event_column_x[index], 103, event_column_x[index + 1], 133),
                      text, FONT_16, (223, 240, 249))
    event_draw.rectangle((0, 570, WIDTH, HEIGHT), fill=(10, 24, 39))
    for box in ((18, 568, 126, 598), (136, 568, 244, 598), (254, 568, 362, 598)):
        draw_button(event_draw, box, "", (26, 68, 96), (69, 165, 205))
    event_image.save(images_dir / "Screen2.png")

    event_down = event_image.copy()
    event_down_draw = ImageDraw.Draw(event_down)
    draw_button(event_down_draw, (375, 17, 530, 67), "常规日志",
                (24, 109, 151), (69, 214, 255), True)
    draw_button(event_down_draw, (676, 17, 835, 67), "正在刷新",
                (180, 112, 22), (248, 183, 66), True)
    draw_button(event_down_draw, (846, 17, 1008, 67), "主菜单",
                (24, 109, 151), (69, 214, 255), True)
    for box in ((18, 568, 126, 598), (136, 568, 244, 598), (254, 568, 362, 598)):
        draw_button(event_down_draw, box, "", (24, 109, 151), (69, 214, 255), True)
    event_down.save(images_dir / "Screen2_down.png")

    regular_image = Image.new("RGB", (WIDTH, HEIGHT), (8, 18, 31))
    regular_draw = ImageDraw.Draw(regular_image)
    regular_draw.rectangle((0, 0, WIDTH, 90), fill=(12, 29, 48))
    regular_draw.rectangle((0, 87, WIDTH, 90), fill=(28, 174, 216))
    draw_button(regular_draw, (210, 17, 365, 67), "事件日志", (39, 63, 82), (89, 125, 148))
    draw_button(regular_draw, (375, 17, 530, 67), "常规日志", (19, 86, 132), (42, 154, 210))
    draw_button(regular_draw, (676, 17, 835, 67), "刷新常规", (19, 86, 132), (42, 154, 210))
    draw_button(regular_draw, (846, 17, 1008, 67), "主菜单", (39, 63, 82), (89, 125, 148))

    regular_draw.rounded_rectangle((16, 100, 1008, 560), radius=10, fill=(10, 27, 44),
                                   outline=(38, 75, 99), width=2)
    regular_draw.rectangle((18, 102, 1006, 134), fill=(20, 55, 82))
    draw_centered(regular_draw, (18, 103, 235, 133), "时间", FONT_16, (223, 240, 249))
    draw_centered(regular_draw, (235, 103, 1006, 133), "记录内容", FONT_16, (223, 240, 249))
    regular_draw.rectangle((0, 570, WIDTH, HEIGHT), fill=(10, 24, 39))
    for box in ((18, 568, 126, 598), (136, 568, 244, 598), (254, 568, 362, 598)):
        draw_button(regular_draw, box, "", (26, 68, 96), (69, 165, 205))
    regular_image.save(images_dir / "Screen3.png")

    regular_down = regular_image.copy()
    regular_down_draw = ImageDraw.Draw(regular_down)
    draw_button(regular_down_draw, (210, 17, 365, 67), "事件日志",
                (24, 109, 151), (69, 214, 255), True)
    draw_button(regular_down_draw, (676, 17, 835, 67), "正在刷新",
                (180, 112, 22), (248, 183, 66), True)
    draw_button(regular_down_draw, (846, 17, 1008, 67), "主菜单",
                (24, 109, 151), (69, 214, 255), True)
    for box in ((18, 568, 126, 598), (136, 568, 244, 598), (254, 568, 362, 598)):
        draw_button(regular_down_draw, box, "", (24, 109, 151), (69, 214, 255), True)
    regular_down.save(images_dir / "Screen3_down.png")


def create_preview(images_dir: Path, project_dir: Path) -> None:
    """
    函数名：create_preview。
    说明：在主背景上绘制一组示例动态数据，用于在下载前检查整体布局。
    输入：images_dir 为背景图所在目录；project_dir 为预览图输出目录。
    输出：无；生成画面预览.png，该文件不参与下载。
    """
    preview = Image.open(images_dir / "Screen1.png").convert("RGBA")
    highlight_frames = build_monitor_highlight_frames()
    preview.alpha_composite(highlight_frames[1], dest=(CARD_X[0], CARD_Y))
    preview.alpha_composite(highlight_frames[2], dest=(CARD_X[2], CARD_Y))
    draw = ImageDraw.Draw(preview)
    pressures = ["2.36", "2.50", "1.42", "--", "3.05", "2.75"]
    states = ["使用", "待用", "低压警告", "停用", "待用", "待用"]

    draw.text((22, 23), "六瓶三阀气源控制系统", font=FONT_28_BOLD,
              fill=(239, 248, 255))
    draw_centered(draw, (689, 34, 763, 66), "2.31", FONT_28_BOLD, (69, 214, 255))
    draw_centered(draw, (826, 38, 1004, 63), "2026-08-21 16:30:00", FONT_16, (223, 240, 249))
    for index, x in enumerate(CARD_X):
        state_color = (245, 249, 255) if index in (0, 2) else ((52, 211, 153) if index in (1, 4, 5) else (155, 171, 181))
        draw_centered(draw, (x + 39, 150, x + 121, 174), states[index], FONT_20, state_color)
        draw_centered(draw, (x + 14, 198, x + 119, 229), pressures[index], FONT_28_BOLD, (69, 214, 255))
        valve_values = ("开启", "关闭", "关闭") if index == 0 else ("关闭", "关闭", "关闭")
        for row, value in zip((250, 284, 318), valve_values):
            color = (52, 211, 153) if value == "开启" else (199, 224, 238)
            draw_centered(draw, (x + 92, row, x + 147, row + 22), value, FONT_18, color)
    preview.convert("RGB").save(project_dir / "画面预览.png")


def create_regular_log_preview(images_dir: Path, project_dir: Path) -> None:
    """
    函数名：create_regular_log_preview。
    说明：按照统一两列滑动控件的实际行数和尺寸绘制常规日志运行效果。
    输入：images_dir为背景图所在目录；project_dir为预览图输出目录。
    输出：无；生成“常规日志实际预览.png”，该文件只用于设计核对，不参与下载。
    """
    preview = Image.open(images_dir / "Screen3.png").convert("RGB")
    draw = ImageDraw.Draw(preview)
    times = ("08-22 10:30", "08-22 10:00", "08-22 09:30", "08-22 09:00", "08-22 08:30",
             "08-22 08:00", "08-22 07:30", "08-22 07:00", "08-22 06:30", "08-22 06:00")
    pressure_rows = (
        "压力:1#3.00 2#4.00 3#5.00 4#19.00 5#8.00 6#9.00 总压:22.00 MPa",
        "压力:1#3.10 2#4.10 3#4.80 4#19.00 5#8.20 6#1.05 总压:22.25 MPa",
        "压力:1#3.20 2#4.30 3#5.00 4#19.00 5#8.30 6#1.50 总压:22.30 MPa",
        "压力:1#3.30 2#4.40 3#5.10 4#19.00 5#8.40 6#2.00 总压:22.60 MPa",
        "压力:1#3.40 2#4.50 3#5.20 4#19.00 5#8.50 6#2.50 总压:22.70 MPa",
        "压力:1#3.50 2#4.60 3#5.30 4#19.00 5#8.60 6#3.00 总压:22.90 MPa",
        "压力:1#3.60 2#4.80 3#5.40 4#19.00 5#8.70 6#3.50 总压:23.00 MPa",
        "压力:1#3.70 2#4.90 3#5.50 4#19.00 5#8.80 6#4.00 总压:23.20 MPa",
        "压力:1#3.80 2#5.00 3#5.60 4#19.00 5#8.90 6#4.50 总压:23.40 MPa",
        "压力:1#3.90 2#5.10 3#5.70 4#19.00 5#9.00 6#4.80 总压:23.50 MPa")
    state_rows = (
        "状态:1#待用 2#待用 3#使用 4#停用 5#待用 6#低压待换",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压警告",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压待换",
        "状态:1#使用 2#待用 3#待用 4#停用 5#待用 6#低压待换")
    grid_color = (38, 75, 99)
    text_color = (223, 240, 249)

    draw.text((548, 27), "常规 12条", font=FONT_14, fill=(69, 214, 255))
    draw.line((235, 135, 235, 558), fill=grid_color, width=1)
    for index, time_text in enumerate(times):
        pressure_top = 135 + round((423 * (index * 2)) / 20)
        pressure_bottom = 135 + round((423 * (index * 2 + 1)) / 20)
        state_bottom = 135 + round((423 * (index * 2 + 2)) / 20)

        draw_centered(draw, (18, pressure_top, 235, pressure_bottom), time_text, FONT_14, text_color)
        draw.text((244, pressure_top + 2), pressure_rows[index], font=FONT_14, fill=text_color)
        draw.text((244, pressure_bottom + 2), state_rows[index], font=FONT_14, fill=text_color)
        draw.line((18, pressure_bottom, 1006, pressure_bottom), fill=grid_color, width=1)
        draw.line((18, state_bottom, 1006, state_bottom), fill=grid_color, width=1)

    preview.save(project_dir / "常规日志实际预览.png")


def add_text_item(root, name: str, control_id: int, text: str, font_id: int,
                  color: str, x: int, y: int, width: int, height: int,
                  text_align: int = 1, text_align_v: int = 0) -> None:
    """
    函数名：add_text_item。
    说明：向画面 XML 添加一个可由 MCU 指令刷新的 GBK 文本显示控件。
    输入：root为画面节点；name和control_id为控件名称与ID；其余参数为文字样式、位置和对齐方式。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": name, "id": str(control_id), "type": "text_display", "text": text,
        "tipinfo": "", "font": str(font_id), "encode": "1", "show_bkg_style": "0",
        "fore_color": color, "bkg_color": "0;0;0", "bkg_image_path": "",
        "xOffset": str(x), "yOffset": str(y), "width": str(width), "height": str(height),
        "input_mode": "0", "variant": "0", "text_type": "0", "text_len_max": "255",
        "password": "0", "focus_rect": "0", "text_align": str(text_align),
        "text_align_v": str(text_align_v),
        "value_limit": "0", "value_precision": "0", "max_value": "100", "min_value": "0",
        "keyboard_init": "0", "keyboard_position": "0", "keyboard_x": "0", "keyboard_y": "0",
        "art_digit": "0", "art_digit_icon": "", "half_width_dot": "0", "bind_variant": "",
        "show_condition": "0", "condition_variant": "", "condition_value": "0"
    })


def add_highlight_icon_item(root, name: str, control_id: int, x: int) -> None:
    """
    函数名：add_highlight_icon_item。
    说明：向实时监控画面添加一个由MCU指定帧的气瓶状态高亮图标控件。
    输入：root为画面节点；name和control_id为控件名称与ID；x为对应气瓶卡片横坐标。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": name, "id": str(control_id), "type": "animation", "icon": "1",
        "play_finish_notify": "0", "press_notify": "3", "step": "20",
        "frame_list": "Images\\Gas_Cylinder_Highlight.icon", "transparent_process": "0",
        "auto_play": "0", "visible": "1", "interval": "1000", "repeat_count": "0",
        "xOffset": str(x), "yOffset": str(CARD_Y), "width": str(CARD_WIDTH),
        "height": str(CARD_HEIGHT), "bind_variant": "", "multi_lang": "0",
        "variant_range": "0", "variant_min": "0", "variant_max": "0",
        "icon_min": "0", "icon_max": "0"
    })


def add_button_item(root, name: str, control_id: int, switch_style: bool,
                    x: int, y: int, width: int, height: int,
                    down_image: str = "Screen1_down.png",
                    button_text: str = "") -> None:
    """
    函数名：add_button_item。
    说明：向画面 XML 添加大彩官方按钮控件，并启用标准控件值上传。
    输入：root 为画面节点；name 和 control_id 为控件名称与 ID；switch_style 指定开关或弹起模式；
          坐标参数指定触摸区；down_image 为按下态背景文件名；button_text 为按钮自身显示文字。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": name, "id": str(control_id), "type": "button", "button_type": "1",
        "focus": "0", "notify_disable": "0", "key_code": "a", "key_type": "0",
        "init_state": "0", "button_style": "1" if switch_style else "0", "longpress_delay": "0",
        "url_down": f"Images\\{down_image}", "url_up": "", "popup_menu_id": "0",
        "input_text_id": "0", "switch": "", "switch_effect": "0", "switch_area": "0",
        "switch_area_left": "0", "switch_area_right": "0", "switch_area_top": "0",
        "switch_area_bottom": "0", "action": "", "xOffset": str(x), "yOffset": str(y),
        "width": str(width), "height": str(height), "cut_up": "0", "cut_up_offset_x": "0",
        "cut_up_offset_y": "0", "cut_down": "1", "cut_down_offset_x": str(x),
        "cut_down_offset_y": str(y), "custom_data_up": "", "custom_data_down": "",
        "external_data_up": "", "external_data_down": "", "external_data_delay": "100",
        "child_screen": "0", "need_login": "0", "login_password": "888888",
        "show_text_state": "1" if button_text else "0", "font": "7",
        "font_color_up": "255;255;255", "font_color_down": "255;255;255",
        "text_state_up": button_text, "text_state_down": button_text,
        "bind_variant": "", "show_condition": "0", "condition_variant": "", "condition_value": "0"
    })


def add_navigation_button(root, name: str, control_id: int, target_screen: str,
                          down_image: str, x: int, y: int, width: int, height: int,
                          need_login: bool = False, login_password: str = "888888") -> None:
    """
    函数名：add_navigation_button。
    说明：添加由VisualTFT内部直接切换画面的导航按钮，不向MCU上传业务事件。
    输入：root为画面节点；name和control_id为控件标识；target_screen为目标画面；其余为按下图和坐标。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": name, "id": str(control_id), "type": "button", "button_type": "0",
        "focus": "0", "notify_disable": "0", "key_code": "a", "key_type": "0",
        "init_state": "0", "button_style": "0", "longpress_delay": "0",
        "url_down": f"Images\\{down_image}", "url_up": "", "popup_menu_id": "0",
        "input_text_id": "0", "switch": target_screen, "switch_effect": "0", "switch_area": "0",
        "switch_area_left": "0", "switch_area_right": "0", "switch_area_top": "0",
        "switch_area_bottom": "0", "action": "", "xOffset": str(x), "yOffset": str(y),
        "width": str(width), "height": str(height), "cut_up": "0", "cut_up_offset_x": "0",
        "cut_up_offset_y": "0", "cut_down": "1", "cut_down_offset_x": str(x),
        "cut_down_offset_y": str(y), "custom_data_up": "", "custom_data_down": "",
        "external_data_up": "", "external_data_down": "", "external_data_delay": "100",
        "child_screen": "0", "need_login": "1" if need_login else "0",
        "login_password": login_password,
        "show_text_state": "0", "font": "7", "font_color_up": "255;255;255",
        "font_color_down": "255;255;255", "text_state_up": "", "text_state_down": "",
        "bind_variant": "", "show_condition": "0", "condition_variant": "", "condition_value": "0"
    })


def add_parameter_input_item(root, name: str, control_id: int,
                             text: str, x: int, y: int) -> None:
    """
    函数名：add_parameter_input_item。
    说明：添加启用大彩系统数字键盘和控件值上传的GBK文本输入控件。
    输入：root为画面节点；name和control_id为控件标识；text为初值；x、y为位置。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": name, "id": str(control_id), "type": "text_display", "text": text,
        "tipinfo": "", "font": "6", "encode": "1", "show_bkg_style": "0",
        "fore_color": "69;214;255", "bkg_color": "0;0;0", "bkg_image_path": "",
        "xOffset": str(x), "yOffset": str(y), "width": "130", "height": "36",
        "input_mode": "1", "variant": "0", "text_type": "0", "text_len_max": "10",
        "password": "0", "focus_rect": "1", "text_align": "1", "text_align_v": "1",
        "value_limit": "0", "value_precision": "3", "max_value": "65535", "min_value": "0",
        "keyboard_init": "1", "keyboard_position": "0", "keyboard_x": "0", "keyboard_y": "0",
        "art_digit": "0", "art_digit_icon": "", "half_width_dot": "0", "bind_variant": "",
        "show_condition": "0", "condition_variant": "", "condition_value": "0"
    })


def add_event_record_item(root) -> None:
    """
    函数名：add_event_record_item。
    说明：添加一屏固定显示15行且不启用内部滚动条的四列事件日志分页控件。
    输入：root为事件日志画面节点。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": "Event_Log_Record", "id": "62", "type": "record",
        "xOffset": "18", "yOffset": "135", "width": "988", "height": "423",
        "font": "4", "font_color": "223;240;249", "show_grid": "1",
        "grid_color": "38;75;99", "show_background": "1", "background_color": "10;27;44",
        "record_type": "3", "new_record_first": "0", "alignment": "0",
        "record_item_count": "4", "record_item_width": "24;10;42;24;",
        "visiable_record_count": "15", "max_record_count": "15", "sizeof_one_record": "96",
        "save_sdram": "1", "flash_block": "0", "record_flash_address": "0",
        "event_enum": "0", "event_value": "", "event_color": "", "event_string": "",
        "event_compare": "", "allow_select": "0", "select_color": "28;174;216",
        "allow_sliding": "0", "icon": "", "hide_vscroll_bar": "1", "show_number": "0"
    })


def add_regular_record_item(root) -> None:
    """
    函数名：add_regular_record_item。
    说明：添加一页20行的两列常规日志控件，每条逻辑记录占压力和状态两行且不启用内部滑动。
    输入：root为常规日志画面节点。
    输出：无。
    """
    ET.SubElement(root, "item", {
        "name": "Regular_Log_Content_Record", "id": "66", "type": "record",
        "xOffset": "18", "yOffset": "135", "width": "988", "height": "423",
        "font": "4", "font_color": "223;240;249", "show_grid": "1",
        "grid_color": "38;75;99", "show_background": "1", "background_color": "10;27;44",
        "record_type": "3", "new_record_first": "0", "alignment": "0",
        "record_item_count": "2", "record_item_width": "22;78;",
        "visiable_record_count": "20", "max_record_count": "20", "sizeof_one_record": "128",
        "save_sdram": "1", "flash_block": "0", "record_flash_address": "0",
        "event_enum": "0", "event_value": "", "event_color": "", "event_string": "",
        "event_compare": "", "allow_select": "0", "select_color": "28;174;216",
        "allow_sliding": "0", "icon": "", "hide_vscroll_bar": "1", "show_number": "0"
    })


def create_monitor_screen_file(project_dir: Path) -> None:
    """
    函数名：create_monitor_screen_file。
    说明：生成包含业务控件、压力双色叠加层、系统启停、导航按钮及72～77号气瓶高亮控件的Screen1实时监控画面。
    输入：project_dir 为大彩工程输出目录。
    输出：无。
    """
    root = ET.Element("DrawPage", {
        "name": "Screen1", "bk_transparent": "0", "bk_color": "8;18;31",
        "bk_image": "Images\\Screen1.png", "size_option": "0", "width": str(WIDTH), "height": str(HEIGHT)
    })
    add_text_item(root, "System_Title", 114, "六瓶三阀气源控制系统", 10,
                  "239;248;255", 22, 23, 390, 34, 0, 1)

    # 高亮图标必须先于文字和按钮加入，使动态数据及触摸控件始终位于高亮图层上方。
    for index, x in enumerate(CARD_X):
        add_highlight_icon_item(root, f"Cylinder_Highlight_{index + 1}", 72 + index, x)

    for index, x in enumerate(CARD_X):
        # 19～24 为正常青色压力，101～106 为同位置的超量程红色压力，25～30 为六瓶状态。
        add_text_item(root, f"Pressure_{index + 1}", 19 + index, "--", 10,
                      "69;214;255", x + 14, 199, 105, 30)
        add_text_item(root, f"Pressure_Overrange_{index + 1}", 101 + index, " ", 10,
                      "255;77;94", x + 14, 199, 105, 30)
        add_text_item(root, f"State_{index + 1}", 25 + index, "初始化", 6,
                      "245;249;255", x + 39, 150, 82, 24)

        # 31～48 依次为六路进气阀、排气阀和测试阀的实时反馈文本。
        add_text_item(root, f"Supply_Valve_{index + 1}", 31 + index, "关闭", 5,
                      "199;224;238", x + 92, 250, 55, 22)
        add_text_item(root, f"Exhaust_Valve_{index + 1}", 37 + index, "关闭", 5,
                      "199;224;238", x + 92, 284, 55, 22)
        add_text_item(root, f"Test_Valve_{index + 1}", 43 + index, "关闭", 5,
                      "199;224;238", x + 92, 318, 55, 22)

        # 排气按钮使用开关外观并由MCU实际排气命令回写；测试、停用和测试通过也使用开关模式。
        add_button_item(root, f"Exhaust_Button_{index + 1}", 1 + index, True,
                        x + 83, 356, 68, 70)
        add_button_item(root, f"Test_Button_{index + 1}", 7 + index, True,
                        x + 9, 438, 68, 70)
        add_button_item(root, f"Disable_Button_{index + 1}", 13 + index, True,
                        x + 83, 438, 68, 70)
        add_button_item(root, f"Qualified_Button_{index + 1}", 51 + index, True,
                        x + 9, 356, 68, 70)

    # 49号与107号在同一位置分别承担正常青色和超量程红色总压力，50号为可校时RTC控件。
    add_text_item(root, "Total_Pressure", 49, "--", 10, "69;214;255", 689, 36, 74, 30)
    add_text_item(root, "Total_Pressure_Overrange", 107, " ", 10,
                  "255;77;94", 689, 36, 74, 30)
    ET.SubElement(root, "item", {
        "name": "System_RTC", "id": "50", "type": "rtc", "lang": "0", "zone": "0",
        "format": "1", "format_string": "%y-%n-%d %h:%m:%s", "press": "1", "font": "6",
        "fore_color": "223;240;249", "input": "1", "xOffset": "826", "yOffset": "39",
        "width": "190", "height": "20", "timer": "60", "increase": "0"
    })

    add_text_item(root, "System_Mode", 94, "自动运行", 5,
                  "52;211;153", 84, 568, 88, 22)
    add_button_item(root, "System_Mode_Switch", 99, True,
                    180, 564, 212, 30, "Screen1_down.png")

    add_navigation_button(root, "Back_To_Menu", 59, "Screen0", "Screen1_down.png",
                          818, 564, 90, 30)
    add_navigation_button(root, "Go_To_Log", 60, "Screen2", "Screen1_down.png",
                          916, 564, 98, 30)

    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / "Screen1.tft", encoding="utf-8", xml_declaration=True)


def create_menu_screen_file(project_dir: Path) -> None:
    """
    函数名：create_menu_screen_file。
    说明：生成作为启动页的Screen0主菜单，导航由串口屏内部完成。
    输入：project_dir为大彩工程输出目录。
    输出：无。
    """
    root = ET.Element("DrawPage", {
        "name": "Screen0", "bk_transparent": "0", "bk_color": "8;18;31",
        "bk_image": "Images\\Screen0.png", "size_option": "0",
        "width": str(WIDTH), "height": str(HEIGHT)
    })
    add_text_item(root, "Menu_System_Title", 115, "六瓶三阀气源控制系统", 10,
                  "239;248;255", 212, 20, 600, 42, 1, 1)
    add_navigation_button(root, "Open_Monitor", 57, "Screen1", "Screen0_down.png",
                          32, 154, 284, 290)
    add_navigation_button(root, "Open_Log", 58, "Screen2", "Screen0_down.png",
                          370, 154, 284, 290)
    add_navigation_button(root, "Open_Config", 78, "Screen4", "Screen0_down.png",
                          708, 154, 284, 290, True, "243579")
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / "Screen0.tft", encoding="utf-8", xml_declaration=True)


def create_event_log_screen_file(project_dir: Path) -> None:
    """
    函数名：create_event_log_screen_file。
    说明：生成Screen2事件日志画面，包含15条分页、刷新、最新页、上一页和下一页按钮。
    输入：project_dir为大彩工程输出目录。
    输出：无。
    """
    root = ET.Element("DrawPage", {
        "name": "Screen2", "bk_transparent": "0", "bk_color": "8;18;31",
        "bk_image": "Images\\Screen2.png", "size_option": "0",
        "width": str(WIDTH), "height": str(HEIGHT)
    })
    add_text_item(root, "Event_Log_Title", 116, "事件日志", 10,
                  "239;248;255", 22, 10, 166, 38, 0, 1)
    add_button_item(root, "Refresh_Event_Log", 61, False, 676, 17, 159, 50,
                    "Screen2_down.png")
    add_event_record_item(root)
    add_text_item(root, "Event_Log_Query_Status", 63, "事件 0条", 4,
                  "69;214;255", 548, 27, 118, 28)
    add_navigation_button(root, "Log_Back_To_Menu", 64, "Screen0", "Screen2_down.png",
                          846, 17, 162, 50)
    add_navigation_button(root, "Event_To_Regular_Log", 69, "Screen3", "Screen2_down.png",
                          375, 17, 155, 50)
    add_button_item(root, "Event_Log_Latest", 119, False, 18, 568, 108, 30,
                    "Screen2_down.png", "最新页")
    add_button_item(root, "Event_Log_Previous", 120, False, 136, 568, 108, 30,
                    "Screen2_down.png", "上一页")
    add_button_item(root, "Event_Log_Next", 121, False, 254, 568, 108, 30,
                    "Screen2_down.png", "下一页")
    add_text_item(root, "Event_Log_Page_Info", 122, "第0/0页 共0条", 4,
                  "163;207;229", 382, 570, 624, 26, 2, 1)
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / "Screen2.tft", encoding="utf-8", xml_declaration=True)


def create_regular_log_screen_file(project_dir: Path) -> None:
    """
    函数名：create_regular_log_screen_file。
    说明：生成Screen3常规日志画面，每页10条并提供刷新、最新页、上一页和下一页按钮。
    输入：project_dir为大彩工程输出目录。
    输出：无。
    """
    root = ET.Element("DrawPage", {
        "name": "Screen3", "bk_transparent": "0", "bk_color": "8;18;31",
        "bk_image": "Images\\Screen3.png", "size_option": "0",
        "width": str(WIDTH), "height": str(HEIGHT)
    })
    add_text_item(root, "Regular_Log_Title", 117, "常规日志", 10,
                  "239;248;255", 22, 10, 166, 38, 0, 1)
    add_button_item(root, "Refresh_Regular_Log", 65, False, 676, 17, 159, 50,
                    "Screen3_down.png")
    add_regular_record_item(root)
    add_text_item(root, "Regular_Log_Query_Status", 67, "常规 0条", 4,
                  "69;214;255", 548, 27, 118, 28)
    add_navigation_button(root, "Regular_Log_Back_To_Menu", 68, "Screen0", "Screen3_down.png",
                          846, 17, 162, 50)
    add_navigation_button(root, "Regular_To_Event_Log", 70, "Screen2", "Screen3_down.png",
                          210, 17, 155, 50)
    add_button_item(root, "Regular_Log_Latest", 123, False, 18, 568, 108, 30,
                    "Screen3_down.png", "最新页")
    add_button_item(root, "Regular_Log_Previous", 124, False, 136, 568, 108, 30,
                    "Screen3_down.png", "上一页")
    add_button_item(root, "Regular_Log_Next", 125, False, 254, 568, 108, 30,
                    "Screen3_down.png", "下一页")
    add_text_item(root, "Regular_Log_Page_Info", 126, "第0/0页 共0条", 4,
                  "163;207;229", 382, 570, 624, 26, 2, 1)
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / "Screen3.tft", encoding="utf-8", xml_declaration=True)


def create_config_screen_file(project_dir: Path) -> None:
    """
    函数名：create_config_screen_file。
    说明：生成Screen4密码参数页，包含11个文本输入、操作提示和两个页面管理按钮。
    输入：project_dir为大彩工程输出目录。
    输出：无。
    """
    root = ET.Element("DrawPage", {
        "name": "Screen4", "bk_transparent": "0", "bk_color": "8;18;31",
        "bk_image": "Images\\Screen4.png", "size_option": "0",
        "width": str(WIDTH), "height": str(HEIGHT)
    })
    add_text_item(root, "Config_Page_Title", 118, "运行参数设置", 10,
                  "239;248;255", 24, 20, 280, 44, 0, 1)
    defaults = ("1.200", "1.500", "2.000", "25.000", "100", "1000",
                "3", "500", "500", "5.000", "60")
    for index, value in enumerate(defaults):
        if index < 6:
            x = 304
            y = 106 + index * 54
        else:
            x = 816
            y = 106 + (index - 6) * 67
        add_parameter_input_item(root, f"Config_Value_{index + 1}", 80 + index, value, x, y)

    add_text_item(root, "Config_Status", 93, "修改任一参数后将自动弹出确认窗口", 4,
                  "69;214;255", 160, 489, 820, 25)
    add_button_item(root, "Config_Default", 97, False, 298, 540, 200, 50, "Screen4_down.png")
    add_navigation_button(root, "Config_Back", 98, "Screen0", "Screen4_down.png",
                          526, 540, 200, 50)
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / "Screen4.tft", encoding="utf-8", xml_declaration=True)


def create_config_dialog_screen_file(project_dir: Path) -> None:
    """
    函数名：create_config_dialog_screen_file。
    说明：生成Screen5独立确认画面，动态显示字段名、当前值、候选值和校验说明。
    输入：project_dir为大彩工程输出目录。
    输出：无。
    """
    root = ET.Element("DrawPage", {
        "name": "Screen5", "bk_transparent": "0", "bk_color": "8;18;31",
        "bk_image": "Images\\Screen5.png", "size_option": "0",
        "width": str(WIDTH), "height": str(HEIGHT)
    })
    add_text_item(root, "Config_Dialog_Name", 110, "压力合法上限", 7,
                  "69;214;255", 300, 218, 424, 34)
    add_text_item(root, "Config_Dialog_Old", 111, "25.000", 7,
                  "223;240;249", 270, 301, 172, 30)
    add_text_item(root, "Config_Dialog_New", 112, "20.000", 7,
                  "52;211;153", 582, 301, 172, 30)
    add_text_item(root, "Config_Dialog_Info", 113, "确认后立即保存并生效", 4,
                  "163;190;207", 250, 357, 524, 24)
    add_button_item(root, "Config_Dialog_Cancel", 109, False,
                    278, 399, 220, 60, "Screen5_down.png", "返回修改")
    add_button_item(root, "Config_Dialog_Confirm", 108, False,
                    526, 399, 220, 60, "Screen5_down.png", "确认并应用")
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / "Screen5.tft", encoding="utf-8", xml_declaration=True)


def create_lua_file(project_dir: Path) -> None:
    """
    函数名：create_lua_file。
    说明：生成控件通知脚本，使用户修改任一参数或点击恢复默认后立即进入独立确认画面。
    输入：project_dir为大彩工程输出目录。
    输出：无；写入VisualTFT工程根目录main.lua。
    """
    script = """-- 参数设置页面控件通知脚本。
-- MCU主动刷新文本不会触发本回调，只有人员输入或点击恢复默认才进入独立确认画面。
function on_control_notify(screen, control, value)
    if screen == 4 then
        if ((control >= 80) and (control <= 90)) or ((control == 97) and (value == 1)) then
            change_screen(5)
        end
    elseif screen == 5 then
        if ((control == 108) or (control == 109)) and (value == 1) then
            change_screen(4)
        end
    end
end
"""
    (project_dir / "main.lua").write_text(script, encoding="utf-8")


def create_project_file(project_dir: Path) -> None:
    """
    函数名：create_project_file。
    说明：生成与现有 DC10600PM101 工程参数一致的 VisualTFT 项目主文件。
    输入：project_dir 为大彩工程输出目录。
    输出：无。
    """
    attributes = {
        "Name": PROJECT_NAME, "OutputDirectory": "output\\", "StartupPage": "Screen0",
        "StartupAction": "", "StartupActionLoop": "1", "DeviceType": "19512", "DeviceEnableControl": "1",
        "DeviceEnableTouchPane": "1", "DeviceEnableBuzzer": "2", "DeviceEnableCRC": "0",
        "DeviceBaudRate": "7", "DeviceCoordinateUpdateMode": "4", "DeviceControlNotify": "3",
        "DeviceScreenNotify": "1", "DeviceScreenRvs": "0", "DeviceBacklightAutoControl": "0",
        "DeviceBacklightTime": "10", "DeviceBacklightOn": "200", "DeviceBacklightOff": "50",
        "DeviceBacklightNotify": "0", "DeviceLockConfig": "1", "DeviceStartupVoice": "0",
        "DeviceVoiceVolume": "100", "DeviceEnableSlidingScreen": "0", "BeginSlidingScreen": "",
        "EndSlidingScreen": "", "CompatibleScreenRotate": "1", "AddResourceFloder": "1",
        "FastRefreshScreen": "0", "LUAPrecompile": "0", "DelayInitScreen": "0",
        "ImageDithering": "1", "ImageFormat": "1", "JPEGQuality": "95", "StartupLogo": "",
        "DHCP": "0", "IPAddress": "192.168.1.100", "NetMask": "255.255.255.0",
        "Gateway": "192.168.1.1", "DNS": "192.168.1.1", "NetworkMode": "0",
        "ServerAddress": "192.168.1.200", "ServicePort": "5050", "NetworkTransfer": "0",
        "WifiMode": "0", "WifiSSID": "", "WifiPassword": "", "FlashType": "2",
        "PartitionSize0": "100", "PartitionSize1": "10", "PartitionSize2": "0", "Repartition": "1",
        "FormatPartition": "1", "UartDownloadSpeed": "0", "PKGSetting": "0", "PKGControl": "1",
        "PKGImage": "1", "PKGFont": "1", "PKGOther": "1", "Version": PROJECT_VERSION,
        "EnableSlidingRange": "0", "EnableKeysound": "0", "BacklightAutoControl": "0",
        "BacklightNotify": "0", "StandbyPage": "", "ServerPort": "5050", "WifiSSI": ""
    }
    root = ET.Element("VisualTFT", attributes)
    pages = ET.SubElement(root, "Pages")
    ET.SubElement(pages, "Page", {"RelativePath": "Screen0.tft"})
    ET.SubElement(pages, "Page", {"RelativePath": "Screen1.tft"})
    ET.SubElement(pages, "Page", {"RelativePath": "Screen2.tft"})
    ET.SubElement(pages, "Page", {"RelativePath": "Screen3.tft"})
    ET.SubElement(pages, "Page", {"RelativePath": "Screen4.tft"})
    ET.SubElement(pages, "Page", {"RelativePath": "Screen5.tft"})
    ET.SubElement(root, "Images")
    ET.SubElement(root, "Waves")
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(project_dir / f"{PROJECT_NAME}.tftprj", encoding="utf-8", xml_declaration=True)


def copy_project_resources(project_dir: Path) -> None:
    """
    函数名：copy_project_resources。
    说明：保留当前正式工程已经验证的GBK字库并建立标准资源目录，Lua脚本由后续函数生成。
    输入：project_dir 为大彩工程输出目录。
    输出：无。
    """
    for folder in ("images", "output", "sounds", "videos"):
        (project_dir / folder).mkdir(parents=True, exist_ok=True)
    if SOURCE_PROJECT.resolve() != project_dir.resolve():
        shutil.copytree(SOURCE_PROJECT / "font", project_dir / "font", dirs_exist_ok=True)
    elif not (project_dir / "font").is_dir():
        raise FileNotFoundError("当前正式大彩工程缺少已验证的font字库目录")
    # 当前版本在原目录内重复生成时不复制自身字库，避免依赖已经删除的历史工程。


def main() -> None:
    """
    函数名：main。
    说明：生成完整的大彩 VisualTFT V1.05正式画面工程。
    输入：无。
    输出：无；所有文件输出到GasControl_HMI_V1.05目录。
    """
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    copy_project_resources(PROJECT_DIR)
    create_menu_backgrounds(PROJECT_DIR / "images")
    create_menu_preview(PROJECT_DIR)
    create_monitor_backgrounds(PROJECT_DIR / "images")
    create_monitor_highlight_icon(PROJECT_DIR / "images")
    create_log_backgrounds(PROJECT_DIR / "images")
    create_config_backgrounds(PROJECT_DIR / "images")
    create_config_dialog_backgrounds(PROJECT_DIR / "images")
    create_preview(PROJECT_DIR / "images", PROJECT_DIR)
    create_regular_log_preview(PROJECT_DIR / "images", PROJECT_DIR)
    create_menu_screen_file(PROJECT_DIR)
    create_monitor_screen_file(PROJECT_DIR)
    create_event_log_screen_file(PROJECT_DIR)
    create_regular_log_screen_file(PROJECT_DIR)
    create_config_screen_file(PROJECT_DIR)
    create_config_dialog_screen_file(PROJECT_DIR)
    create_lua_file(PROJECT_DIR)
    create_project_file(PROJECT_DIR)
    print(PROJECT_DIR)


if __name__ == "__main__":
    main()
