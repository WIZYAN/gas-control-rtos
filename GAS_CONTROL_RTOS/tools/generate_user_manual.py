# /*
#  * Version: v1.12
#  * Author: YXZ
#  * Created: 2026-08-24
#  * Description: 根据Markdown源稿和当前大彩工程画面生成用户操作手册PDF。
#  * History: 无
#  */

from __future__ import annotations

import argparse
import html
import math
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Sequence

from PIL import Image as PilImage
from PIL import ImageDraw, ImageFont
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Image,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


# 工程根目录，用于解析默认输入、输出和大彩工程资源路径。
PROJECT_ROOT = Path(__file__).resolve().parents[1]
# 用户手册的默认Markdown源稿。
DEFAULT_SOURCE = PROJECT_ROOT / "docs" / "用户操作手册_V1.12.md"
# 正式用户手册的默认PDF输出路径。
DEFAULT_OUTPUT = PROJECT_ROOT / "docs" / "用户操作手册_V1.12.pdf"
# 当前正式大彩工程目录。
HMI_PROJECT_DIR = PROJECT_ROOT / "hmi-screen" / "GasControl_HMI_V1.12"
# A4正文左右边距。
PAGE_MARGIN = 15 * mm
# A4正文可用宽度。
CONTENT_WIDTH = A4[0] - (2 * PAGE_MARGIN)
# 手册中串口屏预览图的标准宽度。
SCREEN_WIDTH = CONTENT_WIDTH
# 文档页眉和页脚使用的系统名称。
SYSTEM_NAME = "气源控制系统"
# 文档版本号。
DOCUMENT_VERSION = "V1.12"

# 手册示例值覆盖表，仅改变手册预览，不修改大彩工程源文件。
SCREEN_TEXT_OVERRIDES: dict[int, dict[int, str]] = {
    4: {84: "200"},
}

# 手册配色，保持与大彩工程深色青蓝风格一致。
COLOR_NAVY = colors.HexColor("#0B2034")
COLOR_DARK = colors.HexColor("#10293D")
COLOR_TEAL = colors.HexColor("#197596")
COLOR_TEXT = colors.HexColor("#20384B")
COLOR_MUTED = colors.HexColor("#607D8B")
COLOR_GRID = colors.HexColor("#A9C9D8")
COLOR_ROW = colors.HexColor("#F1F6F9")


@dataclass(frozen=True)
class ManualBlock:
    """Markdown解析后的单个手册内容块。"""

    kind: str
    value: object
    extra: object | None = None


def find_cjk_font() -> Path:
    """查找可用于PDF和截图预览的中文字体，找不到时抛出异常。"""

    # 按优先级排列的中文字体候选路径。
    candidates = (
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/msyh.ttc"),
        HMI_PROJECT_DIR / "output" / "truefont" / "wqymicrohei.ttf",
    )
    # 逐个检查候选字体是否存在。
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("未找到可用中文字体，请安装黑体、微软雅黑或大彩工程字体资源")


def register_pdf_font(font_path: Path) -> str:
    """注册ReportLab中文字体并返回字体名称。"""

    # ReportLab内部使用的统一字体名称。
    font_name = "ManualCJK"
    pdfmetrics.registerFont(TTFont(font_name, str(font_path)))
    return font_name


def remove_source_comments(source_text: str) -> str:
    """删除Markdown源稿中的HTML维护注释。"""

    return re.sub(r"<!--(?!\s*PAGE\s*-->).*?-->", "", source_text, flags=re.DOTALL)


def is_special_line(line: str) -> bool:
    """判断一行是否为Markdown块的起始标记。"""

    stripped = line.strip()
    return (
        not stripped
        or stripped.startswith("#")
        or stripped.startswith("- ")
        or stripped.startswith("|")
        or stripped.startswith("{{")
        or stripped.startswith(":::")
    )


def parse_markdown_page(page_text: str) -> list[ManualBlock]:
    """把单页简化Markdown解析为手册内容块。"""

    # 当前页按行拆分后的源内容。
    lines = page_text.splitlines()
    # 解析得到的内容块列表。
    blocks: list[ManualBlock] = []
    # 当前扫描行号。
    index = 0
    while index < len(lines):
        # 当前行去除首尾空白后的文本。
        line = lines[index].strip()
        if not line:
            index += 1
            continue
        if line.startswith("# "):
            blocks.append(ManualBlock("heading1", line[2:].strip()))
            index += 1
            continue
        if line.startswith("## "):
            blocks.append(ManualBlock("heading2", line[3:].strip()))
            index += 1
            continue
        if line.startswith("{{screen:") and line.endswith("}}"):
            # 单画面指令中的画面编号。
            screen_number = int(line[len("{{screen:") : -2])
            blocks.append(ManualBlock("screen", screen_number))
            index += 1
            continue
        if line.startswith("{{screen-grid:") and line.endswith("}}"):
            # 总览指令中按逗号分隔的画面编号。
            screen_numbers = tuple(
                int(value.strip())
                for value in line[len("{{screen-grid:") : -2].split(",")
            )
            blocks.append(ManualBlock("screen_grid", screen_numbers))
            index += 1
            continue
        if line.startswith(":::") and line != ":::":
            # 提示框起始行中的类型和标题。
            marker = line[3:].strip().split(maxsplit=1)
            callout_type = marker[0]
            callout_title = marker[1] if len(marker) > 1 else "提示"
            # 提示框正文行列表。
            callout_lines: list[str] = []
            index += 1
            while index < len(lines) and lines[index].strip() != ":::":
                if lines[index].strip():
                    callout_lines.append(lines[index].strip())
                index += 1
            if index >= len(lines):
                raise ValueError(f"提示框“{callout_title}”缺少结束标记 :::")
            blocks.append(
                ManualBlock("callout", "".join(callout_lines), (callout_type, callout_title))
            )
            index += 1
            continue
        if line.startswith("- "):
            # 连续项目符号内容。
            bullet_items: list[str] = []
            while index < len(lines) and lines[index].strip().startswith("- "):
                bullet_items.append(lines[index].strip()[2:].strip())
                index += 1
            blocks.append(ManualBlock("bullets", tuple(bullet_items)))
            continue
        if line.startswith("|"):
            # 连续Markdown表格行。
            table_rows: list[list[str]] = []
            while index < len(lines) and lines[index].strip().startswith("|"):
                # 当前表格行按竖线拆分后的单元格。
                cells = [cell.strip() for cell in lines[index].strip().strip("|").split("|")]
                table_rows.append(cells)
                index += 1
            if len(table_rows) < 2:
                raise ValueError("Markdown表格至少需要标题行和分隔行")
            # 第二行只承担Markdown分隔作用，不进入PDF表格。
            blocks.append(ManualBlock("table", (table_rows[0], *table_rows[2:])))
            continue
        # 普通段落的连续文本行。
        paragraph_lines = [line]
        index += 1
        while index < len(lines) and not is_special_line(lines[index]):
            paragraph_lines.append(lines[index].strip())
            index += 1
        blocks.append(ManualBlock("paragraph", "".join(paragraph_lines)))
    return blocks


def parse_manual(source_path: Path) -> list[list[ManualBlock]]:
    """读取手册源稿并按显式分页标记解析全部页面。"""

    # 手册Markdown的完整源文本。
    source_text = source_path.read_text(encoding="utf-8")
    # 去除维护注释后保留的正文文本。
    body_text = remove_source_comments(source_text)
    # 按显式分页标记拆分的页面源文本。
    raw_pages = re.split(r"\n\s*<!--\s*PAGE\s*-->\s*\n", body_text)
    # 每一页对应的内容块列表。
    pages = [parse_markdown_page(page) for page in raw_pages if page.strip()]
    if not pages:
        raise ValueError("用户操作手册源稿没有正文页面")
    return pages


def parse_rgb(value: str) -> tuple[int, int, int]:
    """把大彩工程的分号RGB字符串转换为Pillow颜色元组。"""

    # 分号分隔的三个颜色分量。
    components = tuple(int(component) for component in value.split(";"))
    if len(components) != 3:
        return (220, 240, 248)
    return components


@lru_cache(maxsize=32)
def load_pillow_font(font_path: str, size: int) -> ImageFont.FreeTypeFont:
    """按字号缓存Pillow字体对象。"""

    return ImageFont.truetype(font_path, size=size)


def render_screen_preview(
    screen_number: int,
    output_dir: Path,
    font_path: Path,
) -> Path:
    """由大彩背景和TFT文本控件生成手册用画面预览图。"""

    # 当前画面的背景图片路径。
    background_path = HMI_PROJECT_DIR / "images" / f"Screen{screen_number}.png"
    # 当前画面的TFT控件定义路径。
    tft_path = HMI_PROJECT_DIR / f"Screen{screen_number}.tft"
    if not background_path.is_file() or not tft_path.is_file():
        raise FileNotFoundError(f"缺少Screen{screen_number}背景或TFT控件定义")
    # 可写入动态文本的RGB画面副本。
    preview = PilImage.open(background_path).convert("RGB")
    # Pillow画面绘制对象。
    draw = ImageDraw.Draw(preview)
    # TFT XML根节点。
    root = ET.parse(tft_path).getroot()
    # 当前画面用于手册示例的控件文字覆盖值。
    overrides = SCREEN_TEXT_OVERRIDES.get(screen_number, {})
    for item in root.findall("item"):
        # 当前控件类型。
        item_type = item.get("type", "")
        # 当前文本控件的数字ID。
        control_id = int(item.get("id", "0"))
        if item_type == "text_display":
            # 优先采用手册覆盖值，否则采用TFT初始文本。
            text_value = overrides.get(control_id, item.get("text", ""))
            # TFT文本颜色。
            text_color = parse_rgb(item.get("fore_color", "220;240;248"))
            # TFT横向对齐方式：0左、1中、2右。
            horizontal_align = int(item.get("text_align", "0"))
            # 根据控件高度计算字体时使用的比例。
            font_scale = 0.52
        elif item_type == "button" and item.get("show_text_state") == "1":
            # 需要由控件层显示的按钮初始文字。
            text_value = item.get("text_state_up", "")
            # 按钮抬起状态的文字颜色。
            text_color = parse_rgb(item.get("font_color_up", "255;255;255"))
            horizontal_align = 1
            font_scale = 0.36
        else:
            continue
        if not text_value:
            continue
        # 文本控件左上角横坐标。
        x_offset = int(item.get("xOffset", "0"))
        # 文本控件左上角纵坐标。
        y_offset = int(item.get("yOffset", "0"))
        # 文本控件宽度。
        control_width = int(item.get("width", "1"))
        # 文本控件高度。
        control_height = int(item.get("height", "1"))
        # 根据控件高度估算的预览字体字号。
        font_size = max(12, min(30, int(control_height * font_scale)))
        # 当前控件使用的Pillow字体对象。
        control_font = load_pillow_font(str(font_path), font_size)
        # 文本绘制锚点横坐标。
        if horizontal_align == 1:
            text_x = x_offset + (control_width / 2)
            anchor = "mm"
        elif horizontal_align == 2:
            text_x = x_offset + control_width - 2
            anchor = "rm"
        else:
            text_x = x_offset + 2
            anchor = "lm"
        # 文本绘制锚点纵坐标。
        text_y = y_offset + (control_height / 2)
        draw.text((text_x, text_y), text_value, fill=text_color, font=control_font, anchor=anchor)
    # 生成后的临时预览图片路径。
    preview_path = output_dir / f"Screen{screen_number}_manual.png"
    preview.save(preview_path, format="PNG")
    return preview_path


def make_styles(font_name: str) -> dict[str, ParagraphStyle]:
    """创建用户手册使用的全部段落样式。"""

    # ReportLab默认样式表，用作样式继承基础。
    sample_styles = getSampleStyleSheet()
    return {
        "title": ParagraphStyle(
            "ManualTitle",
            parent=sample_styles["Heading1"],
            fontName=font_name,
            fontSize=20,
            leading=27,
            textColor=COLOR_DARK,
            spaceAfter=10,
        ),
        "heading2": ParagraphStyle(
            "ManualHeading2",
            parent=sample_styles["Heading2"],
            fontName=font_name,
            fontSize=13,
            leading=18,
            textColor=COLOR_TEAL,
            spaceBefore=4,
            spaceAfter=6,
        ),
        "body": ParagraphStyle(
            "ManualBody",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=8.8,
            leading=14,
            wordWrap="CJK",
            textColor=COLOR_TEXT,
            spaceAfter=5,
        ),
        "caption": ParagraphStyle(
            "ManualCaption",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=7.5,
            leading=11,
            alignment=TA_CENTER,
            textColor=COLOR_MUTED,
            spaceBefore=2,
            spaceAfter=6,
        ),
        "bullet": ParagraphStyle(
            "ManualBullet",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=8.6,
            leading=13,
            leftIndent=9,
            firstLineIndent=-7,
            wordWrap="CJK",
            textColor=COLOR_TEXT,
            spaceAfter=2,
        ),
        "table": ParagraphStyle(
            "ManualTable",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=7.3,
            leading=10.2,
            wordWrap="CJK",
            textColor=COLOR_TEXT,
        ),
        "table_header": ParagraphStyle(
            "ManualTableHeader",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=7.6,
            leading=10.5,
            alignment=TA_CENTER,
            wordWrap="CJK",
            textColor=colors.white,
        ),
        "callout": ParagraphStyle(
            "ManualCallout",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=8.5,
            leading=13,
            wordWrap="CJK",
            textColor=COLOR_TEXT,
        ),
        "cover_title": ParagraphStyle(
            "ManualCoverTitle",
            parent=sample_styles["Title"],
            fontName=font_name,
            fontSize=26,
            leading=34,
            alignment=TA_CENTER,
            textColor=colors.white,
        ),
        "cover_subtitle": ParagraphStyle(
            "ManualCoverSubtitle",
            parent=sample_styles["BodyText"],
            fontName=font_name,
            fontSize=11,
            leading=17,
            alignment=TA_CENTER,
            textColor=colors.HexColor("#9ED8F2"),
        ),
    }


def escape_text(value: str) -> str:
    """转义ReportLab段落中的特殊字符。"""

    return html.escape(value, quote=False).replace("\n", "<br/>")


def calculate_column_widths(rows: Sequence[Sequence[str]]) -> list[float]:
    """根据各列文本长度计算适合A4页面的表格列宽。"""

    # 表格列数。
    column_count = len(rows[0])
    # 每列最大文本长度形成的原始权重。
    raw_weights = [
        max(7.0, math.sqrt(max(len(row[column]) for row in rows)) * 3.2)
        for column in range(column_count)
    ]
    # 所有列权重总和。
    total_weight = sum(raw_weights)
    # 按权重分配后的列宽。
    widths = [CONTENT_WIDTH * weight / total_weight for weight in raw_weights]
    # 防止短标题列过窄的最小列宽。
    minimum_width = CONTENT_WIDTH * (0.10 if column_count >= 5 else 0.13)
    for column, width in enumerate(widths):
        if width < minimum_width:
            widths[column] = minimum_width
    # 若设置最小宽度后超宽，则按比例缩回正文宽度。
    scale = CONTENT_WIDTH / sum(widths)
    return [width * scale for width in widths]


def build_table(
    rows: Sequence[Sequence[str]],
    styles: dict[str, ParagraphStyle],
) -> Table:
    """把Markdown表格数据转换为统一样式的ReportLab表格。"""

    # 转换为ReportLab段落后的单元格数据。
    formatted_rows: list[list[Paragraph]] = []
    for row_index, row in enumerate(rows):
        # 标题行和正文行分别使用的段落样式。
        cell_style = styles["table_header"] if row_index == 0 else styles["table"]
        formatted_rows.append([Paragraph(escape_text(cell), cell_style) for cell in row])
    # 按文本长度计算的列宽列表。
    column_widths = calculate_column_widths(rows)
    # 最终表格对象。
    table = Table(formatted_rows, colWidths=column_widths, repeatRows=1, hAlign="LEFT")
    # 统一表格颜色、边框、内边距和交替行底色。
    commands: list[tuple] = [
        ("BACKGROUND", (0, 0), (-1, 0), COLOR_TEAL),
        ("GRID", (0, 0), (-1, -1), 0.45, COLOR_GRID),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]
    for row_index in range(2, len(rows), 2):
        commands.append(("BACKGROUND", (0, row_index), (-1, row_index), COLOR_ROW))
    table.setStyle(TableStyle(commands))
    return table


def build_callout(
    text_value: str,
    callout_meta: tuple[str, str],
    styles: dict[str, ParagraphStyle],
) -> Table:
    """创建警告、提示、成功或危险提示框。"""

    # 提示框类型和标题。
    callout_type, callout_title = callout_meta
    # 不同提示类型对应的边框色和浅色背景。
    palette = {
        "info": (colors.HexColor("#14739A"), colors.HexColor("#E8F5FA")),
        "warning": (colors.HexColor("#EF9B20"), colors.HexColor("#FFF6E8")),
        "success": (colors.HexColor("#159B68"), colors.HexColor("#EAF9F2")),
        "danger": (colors.HexColor("#D33F55"), colors.HexColor("#FDECEF")),
    }
    # 未知类型按普通提示框处理。
    border_color, background_color = palette.get(callout_type, palette["info"])
    # 提示框内的标题和正文段落。
    paragraph = Paragraph(
        f"<b>{escape_text(callout_title)}</b><br/>{escape_text(text_value)}",
        styles["callout"],
    )
    # 单单元格提示框表格。
    callout = Table([[paragraph]], colWidths=[CONTENT_WIDTH], hAlign="LEFT")
    callout.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), background_color),
                ("BOX", (0, 0), (-1, -1), 0.7, border_color),
                ("LINEBEFORE", (0, 0), (0, -1), 3.2, border_color),
                ("LEFTPADDING", (0, 0), (-1, -1), 10),
                ("RIGHTPADDING", (0, 0), (-1, -1), 9),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]
        )
    )
    return callout


def build_screen_image(screen_path: Path, width: float = SCREEN_WIDTH) -> Image:
    """创建保持1024×600比例的ReportLab画面图片。"""

    return Image(str(screen_path), width=width, height=width * 600 / 1024)


def build_screen_grid(screen_paths: Sequence[Path], styles: dict[str, ParagraphStyle]) -> Table:
    """把八个串口屏画面排成两列四行总览。"""

    # 总览中单张画面的宽度。
    image_width = (CONTENT_WIDTH - 12) / 2
    # 总览表格的单元格内容。
    cells: list[list[object]] = []
    for row_start in range(0, len(screen_paths), 2):
        # 当前总览行的两个画面单元格。
        row_cells: list[object] = []
        for column_offset in range(2):
            # 当前单元格对应的画面列表索引。
            screen_index = row_start + column_offset
            if screen_index >= len(screen_paths):
                row_cells.append([Spacer(1, 1)])
                continue
            # 从预览文件名解析出的画面编号。
            screen_number = int(re.search(r"Screen(\d+)", screen_paths[screen_index].name).group(1))
            row_cells.append(
                [
                    build_screen_image(screen_paths[screen_index], image_width),
                    Paragraph(f"Screen{screen_number}", styles["caption"]),
                ]
            )
        cells.append(row_cells)
    # 画面总览表格。
    grid = Table(cells, colWidths=[image_width, image_width], hAlign="CENTER")
    grid.setStyle(
        TableStyle(
            [
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 2),
                ("RIGHTPADDING", (0, 0), (-1, -1), 2),
                ("TOPPADDING", (0, 0), (-1, -1), 1),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 1),
            ]
        )
    )
    return grid


def build_cover(
    blocks: Sequence[ManualBlock],
    screen_paths: dict[int, Path],
    styles: dict[str, ParagraphStyle],
) -> list[object]:
    """生成封面页流式内容。"""

    # 封面一级标题。
    title = next(str(block.value) for block in blocks if block.kind == "heading1")
    # 封面标题后的第一个普通段落作为副标题。
    subtitle = next(str(block.value) for block in blocks if block.kind == "paragraph")
    # 封面元数据表格。
    metadata_rows = next(block.value for block in blocks if block.kind == "table")
    # 深色封面标题区。
    title_panel = Table(
        [[Paragraph(escape_text(title), styles["cover_title"])],
         [Paragraph(escape_text(subtitle), styles["cover_subtitle"])]],
        colWidths=[CONTENT_WIDTH],
        rowHeights=[54, 35],
    )
    title_panel.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), COLOR_NAVY),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    # 封面画面说明文字。
    cover_caption = "当前正式主菜单画面（Screen0）"
    return [
        title_panel,
        Spacer(1, 14),
        build_screen_image(screen_paths[0]),
        Paragraph(cover_caption, styles["caption"]),
        build_table(metadata_rows, styles),
    ]


def build_page_flowables(
    blocks: Sequence[ManualBlock],
    screen_paths: dict[int, Path],
    styles: dict[str, ParagraphStyle],
) -> list[object]:
    """把普通页面内容块转换为ReportLab流式对象。"""

    # 当前页生成的流式对象列表。
    flowables: list[object] = []
    # 上一个内容块类型，用于识别画面后的说明文字。
    previous_kind = ""
    for block in blocks:
        if block.kind == "heading1":
            flowables.append(Paragraph(escape_text(str(block.value)), styles["title"]))
        elif block.kind == "heading2":
            flowables.append(Paragraph(escape_text(str(block.value)), styles["heading2"]))
        elif block.kind == "paragraph":
            # 紧跟画面后的普通段落按图片说明样式显示。
            paragraph_style = styles["caption"] if previous_kind == "screen" else styles["body"]
            flowables.append(Paragraph(escape_text(str(block.value)), paragraph_style))
        elif block.kind == "bullets":
            for item in block.value:
                flowables.append(Paragraph(f"- {escape_text(str(item))}", styles["bullet"]))
            flowables.append(Spacer(1, 3))
        elif block.kind == "table":
            flowables.append(build_table(block.value, styles))
            flowables.append(Spacer(1, 7))
        elif block.kind == "callout":
            flowables.append(build_callout(str(block.value), block.extra, styles))
            flowables.append(Spacer(1, 7))
        elif block.kind == "screen":
            flowables.append(build_screen_image(screen_paths[int(block.value)]))
        elif block.kind == "screen_grid":
            # 按源稿指定顺序取得总览画面路径。
            ordered_paths = [screen_paths[int(number)] for number in block.value]
            flowables.append(build_screen_grid(ordered_paths, styles))
        else:
            raise ValueError(f"不支持的手册内容块：{block.kind}")
        previous_kind = block.kind
    return flowables


def draw_header_footer(canvas_object, document) -> None:
    """绘制普通页面的页眉、页脚和页码。"""

    # 当前正在生成的PDF页码。
    page_number = canvas_object.getPageNumber()
    if page_number == 1:
        return
    canvas_object.saveState()
    # 页眉分隔线。
    canvas_object.setStrokeColor(COLOR_GRID)
    canvas_object.setLineWidth(0.45)
    canvas_object.line(PAGE_MARGIN, A4[1] - 12 * mm, A4[0] - PAGE_MARGIN, A4[1] - 12 * mm)
    # 页眉左侧文档名称。
    canvas_object.setFillColor(COLOR_MUTED)
    canvas_object.setFont(document.manual_font_name, 7.5)
    canvas_object.drawString(PAGE_MARGIN, A4[1] - 9.2 * mm, "用户操作手册")
    # 页眉右侧版本号。
    canvas_object.drawRightString(A4[0] - PAGE_MARGIN, A4[1] - 9.2 * mm, DOCUMENT_VERSION)
    # 页脚分隔线。
    canvas_object.line(PAGE_MARGIN, 13 * mm, A4[0] - PAGE_MARGIN, 13 * mm)
    canvas_object.drawString(PAGE_MARGIN, 8.8 * mm, f"{SYSTEM_NAME} · 面向现场操作与维护人员")
    canvas_object.drawRightString(A4[0] - PAGE_MARGIN, 8.8 * mm, f"第 {page_number} 页")
    canvas_object.restoreState()


def generate_manual(source_path: Path, output_path: Path) -> int:
    """生成用户操作手册PDF，成功返回0，失败由调用者处理异常。"""

    # 生成PDF和预览图使用的中文字体路径。
    font_path = find_cjk_font()
    # ReportLab注册后的字体名称。
    font_name = register_pdf_font(font_path)
    # Markdown源稿解析后的全部页面。
    pages = parse_manual(source_path)
    if len(pages) != 20:
        raise ValueError(f"用户操作手册应为20个显式页面，当前解析到{len(pages)}页")
    # 正式输出路径的父目录。
    output_path.parent.mkdir(parents=True, exist_ok=True)
    # 临时目录会在PDF生成结束后自动清理。
    with tempfile.TemporaryDirectory(prefix="gas_manual_") as temporary_directory:
        # 临时画面预览目录。
        preview_dir = Path(temporary_directory)
        # Screen0～Screen7的合成预览图片路径表。
        screen_paths = {
            screen_number: render_screen_preview(screen_number, preview_dir, font_path)
            for screen_number in range(8)
        }
        # 全部ReportLab段落样式。
        styles = make_styles(font_name)
        # A4 PDF文档对象。
        document = SimpleDocTemplate(
            str(output_path),
            pagesize=A4,
            leftMargin=PAGE_MARGIN,
            rightMargin=PAGE_MARGIN,
            topMargin=16 * mm,
            bottomMargin=16 * mm,
            title="用户操作手册",
            author="气源控制系统项目",
            subject="气源控制系统V1.12用户操作与维护说明",
            pageCompression=1,
        )
        # 页眉页脚回调读取的已注册字体名。
        document.manual_font_name = font_name
        # 最终写入PDF的全部流式对象。
        story: list[object] = []
        story.extend(build_cover(pages[0], screen_paths, styles))
        for page_blocks in pages[1:]:
            story.append(PageBreak())
            story.extend(build_page_flowables(page_blocks, screen_paths, styles))
        document.build(story, onFirstPage=draw_header_footer, onLaterPages=draw_header_footer)
    return 0


def parse_arguments(arguments: Sequence[str]) -> argparse.Namespace:
    """解析命令行参数。"""

    # 用户手册生成器的命令行参数解析器。
    parser = argparse.ArgumentParser(description="生成气源控制系统V1.12用户操作手册PDF")
    parser.add_argument(
        "--source",
        type=Path,
        default=DEFAULT_SOURCE,
        help=f"Markdown源稿路径，默认：{DEFAULT_SOURCE}",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"PDF输出路径，默认：{DEFAULT_OUTPUT}",
    )
    return parser.parse_args(arguments)


def main(arguments: Sequence[str] | None = None) -> int:
    """执行用户手册生成流程并返回进程退出码。"""

    # 实际参与解析的命令行参数。
    parsed_arguments = parse_arguments(sys.argv[1:] if arguments is None else arguments)
    try:
        return generate_manual(
            parsed_arguments.source.resolve(),
            parsed_arguments.output.resolve(),
        )
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"生成用户操作手册失败：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
