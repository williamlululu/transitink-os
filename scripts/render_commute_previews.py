#!/usr/bin/env python3
"""Render deterministic 400x300 monochrome Yue Wan commute previews."""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = (
    ROOT
    / "third_party"
    / "fonts"
    / "noto-sans-cjk-hk"
    / "NotoSansCJKhk-Regular.otf"
)
WIDTH = 400
HEIGHT = 300
BLACK = 0
WHITE = 255


@dataclass(frozen=True)
class Scenario:
    filename: str
    time: str
    date: str
    temperature: str
    rain: str
    phase: str
    recommendation: str
    leave: str
    arrival: str
    status: str
    margin: str
    route_a_status: str
    route_a_bus: str
    route_a_detail: str
    route_b_status: str
    route_b_bus: str
    route_b_detail: str
    warning: bool
    headline: str
    fallback: str
    quality: str


SCENARIOS = (
    Scenario(
        "01_route_a_on_time.png", "06:10", "星期四 9月3日", "24°C",
        "現時無雨／今日有雨機會低", "通勤更新", "106→8P", "06:10",
        "07:03", "準時", "早22分", "準時", "下一班 106 06:16　8P 06:36　轉車餘裕 3分",
        "最遲出門 06:10　預計到達 07:03　錯過首班 07:13到", "準時",
        "下一班 118 06:29　步行 13分",
        "最遲出門 06:14　預計到達 07:04　錯過首班 07:17到", False,
        "建議路線A；轉車餘裕充足", "錯過首班：預計07:13到／仍可趕及",
        "資料正常／通勤更新",
    ),
    Scenario(
        "02_route_b_rain.png", "06:28", "星期四 9月3日", "25°C",
        "現時有雨／今日有雨機會高", "通勤更新", "118", "立即", "07:20",
        "準時", "早5分", "有風險", "下一班 106 06:36　8P 06:57　轉車餘裕 4分",
        "最遲出門 06:30　預計到達 07:24　錯過首班 07:42到", "準時",
        "下一班 118 06:45　步行 13分",
        "最遲出門 06:30　預計到達 07:20　錯過首班 07:34到", False,
        "下雨：建議直接乘118，少一次轉車", "錯過首班：預計07:34到／將會遲到",
        "資料正常／通勤更新",
    ),
    Scenario(
        "03_recovery_0710.png", "07:12", "星期四 9月3日", "26°C",
        "現時無雨／今日有雨機會中低", "遲到應變", "118", "立即", "08:03",
        "將會遲到", "遲38分", "暫無班次", "暫無可趕及的106及8P轉車組合",
        "請直接比較最快可用班次", "將會遲到", "下一班 118 07:28　步行 13分",
        "最遲出門 07:13　預計到達 08:03　錯過首班 08:16到", True,
        "警告：預計將會遲到", "錯過首班：預計08:16到／將會遲到",
        "資料正常／遲到應變",
    ),
    Scenario(
        "04_stale_unavailable.png", "06:50", "星期四 9月3日", "24°C*",
        "現時無雨*／今日有雨機會中*", "密集更新", "--", "--:--", "--:--",
        "暫無班次", "未能估算", "暫無班次", "暫無可趕及的106及8P轉車組合",
        "最近一次資料已超過三分鐘", "暫無班次", "暫無可趕及的118班次",
        "Wi-Fi中斷；保留最後畫面並重試", True,
        "警告：暫無可靠路線，07:25難以確定", "錯過首班：後續結果未明",
        "網絡中斷／密集更新／資料過時",
    ),
    Scenario(
        "05_manual_weekend.png", "06:15", "星期日 9月6日", "25°C",
        "現時無雨／今日有雨機會低", "手動更新", "106→8P", "立即", "07:10",
        "準時", "早15分", "準時", "下一班 106 06:21　8P 06:43　轉車餘裕 5分",
        "最遲出門 06:15　預計到達 07:10　錯過首班 07:22到", "有風險",
        "下一班 118 06:38　步行 13分",
        "最遲出門 06:23　預計到達 07:13　錯過首班 07:29到", False,
        "週末手動查詢；十分鐘後返回待機", "錯過首班：預計07:22到／仍可趕及",
        "資料正常／手動更新",
    ),
)


class PreviewRenderer:
    def __init__(self) -> None:
        if not FONT_PATH.is_file():
            raise FileNotFoundError(f"missing pinned preview font: {FONT_PATH}")
        self.fonts: dict[int, ImageFont.FreeTypeFont] = {}

    def font(self, size: int) -> ImageFont.FreeTypeFont:
        if size not in self.fonts:
            self.fonts[size] = ImageFont.truetype(str(FONT_PATH), size=size)
        return self.fonts[size]

    def fit(self, draw: ImageDraw.ImageDraw, text: str, size: int, width: int) -> str:
        font = self.font(size)
        if draw.textlength(text, font=font) <= width:
            return text
        suffix = "…"
        result = text
        while result and draw.textlength(result + suffix, font=font) > width:
            result = result[:-1]
        return result + suffix if result else suffix

    def text(
        self,
        draw: ImageDraw.ImageDraw,
        xy: tuple[int, int],
        value: str,
        size: int,
        fill: int,
        max_width: int,
        anchor: str = "la",
    ) -> None:
        x, y = xy
        assert 0 <= x < WIDTH and 0 <= y < HEIGHT
        fitted = self.fit(draw, value, size, max_width)
        draw.text(xy, fitted, font=self.font(size), fill=fill, anchor=anchor)
        bbox = draw.textbbox(xy, fitted, font=self.font(size), anchor=anchor)
        assert bbox[0] >= 0 and bbox[1] >= 0
        assert bbox[2] <= WIDTH and bbox[3] <= HEIGHT

    def render(self, scenario: Scenario) -> Image.Image:
        image = Image.new("L", (WIDTH, HEIGHT), WHITE)
        draw = ImageDraw.Draw(image)

        draw.rectangle((0, 0, WIDTH - 1, 35), fill=BLACK)
        self.text(draw, (8, 2), "漁灣邨 07:25前到達", 15, WHITE, 178)
        self.text(draw, (8, 19), scenario.date, 11, WHITE, 112)
        self.text(draw, (188, 2), f"氣溫 {scenario.temperature}", 13, WHITE, 92)
        self.text(draw, (126, 20), scenario.rain, 10, WHITE, 180)
        draw.arc((271, 7, 287, 23), 205, 335, fill=WHITE, width=2)
        draw.ellipse((278, 21, 280, 23), fill=WHITE)
        draw.rectangle((291, 7, 311, 24), outline=WHITE, width=1)
        draw.rectangle((312, 12, 314, 19), fill=WHITE)
        draw.rectangle((294, 10, 305, 21), fill=WHITE)
        self.text(draw, (318, 2), scenario.time, 21, WHITE, 62)
        self.text(draw, (379, 2), "1/2", 8, WHITE, 20)

        draw.rectangle((0, 36, WIDTH - 1, 107), fill=BLACK)
        if scenario.phase == "遲到應變":
            hero_label = "遲到應變／最快選擇"
        else:
            hero_label = "建議"
        if scenario.recommendation == "--":
            self.text(draw, (12, 49), "暫無可靠即時路線", 21, WHITE, 376)
            self.text(draw, (12, 82), "請提早出門並查看實際班次", 14, WHITE, 376)
        else:
            self.text(draw, (10, 41), hero_label, 13, WHITE, 192)
            self.text(draw, (10, 60), scenario.recommendation, 30, WHITE, 190)
            self.text(draw, (214, 41), "最遲出門", 12, WHITE, 86)
            self.text(draw, (214, 59), scenario.leave, 20, WHITE, 86)
            self.text(draw, (310, 41), "預計到達", 12, WHITE, 82)
            self.text(draw, (310, 59), scenario.arrival, 20, WHITE, 82)
            self.text(
                draw, (214, 87), f"{scenario.status}／{scenario.margin}", 12, WHITE, 176
            )

        self._route(
            draw, 108, "A  106→8P　紅磡街市→維園轉車→漁灣邨",
            scenario.route_a_status, scenario.route_a_bus, scenario.route_a_detail,
        )
        self._route(
            draw, 170, "B  118　海底隧道→漁灣邨",
            scenario.route_b_status, scenario.route_b_bus, scenario.route_b_detail,
        )

        footer_fill = BLACK if scenario.warning else WHITE
        footer_text = WHITE if scenario.warning else BLACK
        draw.rectangle((0, 232, WIDTH - 1, HEIGHT - 1), fill=footer_fill)
        self.text(draw, (10, 234), scenario.headline, 13, footer_text, 380)
        self.text(draw, (10, 250), scenario.fallback, 12, footer_text, 380)
        self.text(draw, (10, 265), scenario.quality + f"　更新於 {scenario.time}",
                  11, footer_text, 380)
        self.text(
            draw, (10, 282), "行車時間採保守估算　音量下鍵：天氣",
            11, footer_text, 380,
        )
        return image.convert("1", dither=Image.Dither.NONE)

    def _route(
        self,
        draw: ImageDraw.ImageDraw,
        top: int,
        title: str,
        status: str,
        buses: str,
        detail: str,
    ) -> None:
        draw.line((0, top + 61, WIDTH - 1, top + 61), fill=BLACK, width=1)
        self.text(draw, (10, top + 2), title, 13, BLACK, 306)
        self.text(draw, (390, top + 2), status, 12, BLACK, 74, anchor="ra")
        self.text(draw, (10, top + 23), buses, 11, BLACK, 380)
        self.text(draw, (10, top + 42), detail, 11, BLACK, 380)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist" / "previews")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    renderer = PreviewRenderer()
    manifest: list[dict[str, object]] = []
    for scenario in SCENARIOS:
        image = renderer.render(scenario)
        if args.self_test:
            assert image.size == (WIDTH, HEIGHT)
            assert image.mode == "1"
            assert image.getbbox() is not None
        path = output_dir / scenario.filename
        image.save(path, format="PNG", optimize=False)
        manifest.append(
            {
                "file": scenario.filename,
                "width": WIDTH,
                "height": HEIGHT,
                "mode": "1-bit monochrome",
                "sha256": sha256(path),
            }
        )
    (output_dir / "preview-manifest.json").write_text(
        json.dumps({"version": 1, "previews": manifest}, ensure_ascii=False, indent=2)
        + "\n",
        encoding="utf-8",
    )
    print(f"Rendered {len(manifest)} previews to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
