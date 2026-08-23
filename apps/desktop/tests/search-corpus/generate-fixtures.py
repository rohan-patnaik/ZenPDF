#!/usr/bin/env python3
"""Generate the source-owned ZenPDF search corpus outside the repository.

This optional provenance helper requires ReportLab and Pillow. Producer metadata may
change byte hashes between runs; SHA256SUMS identifies the reviewed corpus itself.
"""

from __future__ import annotations

import io
import zlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4, letter
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "fixtures"
OUT.mkdir(exist_ok=True)
FONT_PATH = "/usr/share/fonts/noto/NotoSans-Regular.ttf"
pdfmetrics.registerFont(TTFont("NotoSans", FONT_PATH))


def unicode_pdf(path: Path) -> None:
    document = canvas.Canvas(str(path), pagesize=A4, pageCompression=1)
    document.setTitle("Unicode search interoperability fixture")
    document.setAuthor("ZenPDF search corpus via ReportLab")
    lines = [
        "ASCII sentinel: zenpdf-unicode-sentinel",
        "Composed Latin: café naïve Straße",
        "Decomposed Latin: café naïve",
        "Greek: Σίσυφος Αθήνα",
        "Cyrillic: Привет Москва",
        "Ligature probe: office efficient affinity ﬃ",
        "Case probes: QUOKKA quokka QuOkKa",
        "Punctuation: co-operate C++ foo.bar@example.test",
    ]
    document.setFont("NotoSans", 14)
    y = 790
    for text in lines:
        document.drawString(48, y, text)
        y -= 42
    document.showPage()
    document.setFont("NotoSans", 14)
    document.drawString(48, 790, "Page 2 control: no target from page 1 should leak here")
    document.save()


def image_only_pdf(path: Path) -> None:
    image = Image.new("RGB", (1600, 1000), "white")
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT_PATH, 54)
    draw.text((100, 120), "VISIBLE IMAGE-ONLY TOKEN: wombat", fill="black", font=font)
    draw.rectangle((80, 100, 1510, 240), outline="navy", width=5)
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    buffer.seek(0)
    document = canvas.Canvas(str(path), pagesize=letter, pageCompression=1)
    document.drawImage(ImageReader(buffer), 36, 300, width=540, height=337.5)
    document.save()


def long_search_pdf(path: Path, pages: int = 400) -> None:
    document = canvas.Canvas(str(path), pagesize=letter, pageCompression=1)
    document.setFont("Helvetica", 11)
    for page in range(1, pages + 1):
        document.drawString(54, 730, f"Long document page {page} of {pages}")
        token = "long-search-target-399" if page == pages - 1 else f"ordinary-page-{page}"
        document.drawString(54, 690, token)
        if page != pages:
            document.showPage()
            document.setFont("Helvetica", 11)
    document.save()


def many_results_pdf(path: Path, pages: int = 100, rows: int = 40) -> None:
    document = canvas.Canvas(str(path), pagesize=letter, pageCompression=1)
    document.setFont("Helvetica", 8)
    for page in range(1, pages + 1):
        document.drawString(36, 760, f"Result flood page {page}")
        for row in range(rows):
            document.drawString(36, 740 - row * 17, f"needle result p{page:03d} r{row:02d}")
        if page != pages:
            document.showPage()
            document.setFont("Helvetica", 8)
    document.save()


def raw_pdf(path: Path, objects: list[bytes]) -> None:
    data = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for number, body in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f"{number} 0 obj\n".encode())
        data.extend(body)
        data.extend(b"\nendobj\n")
    startxref = len(data)
    data.extend(f"xref\n0 {len(objects) + 1}\n".encode())
    data.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        data.extend(f"{offset:010d} 00000 n \n".encode())
    data.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{startxref}\n%%EOF\n".encode()
    )
    path.write_bytes(data)


def huge_page_box_pdf(path: Path) -> None:
    content = b"BT /F1 12 Tf 72 72 Td (huge-page-box-control) Tj ET"
    raw_pdf(path, [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 1000000000 1000000000] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"\nendstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ])


def flate_expansion_pdf(path: Path) -> None:
    expanded = b"%" + (b"A" * (32 * 1024 * 1024)) + b"\nBT /F1 12 Tf 72 72 Td (flate-expansion-control) Tj ET"
    compressed = zlib.compress(expanded, level=9)
    raw_pdf(path, [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        b"<< /Length " + str(len(compressed)).encode()
        + b" /Filter /FlateDecode >>\nstream\n" + compressed + b"\nendstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ])


unicode_pdf(OUT / "unicode-search-independent.pdf")
image_only_pdf(OUT / "image-only-no-text-layer.pdf")
long_search_pdf(OUT / "long-search-400-pages.pdf")
many_results_pdf(OUT / "many-results-4000-matches.pdf")
huge_page_box_pdf(OUT / "hostile-huge-page-box.pdf")
flate_expansion_pdf(OUT / "hostile-flate-expansion-32m.pdf")

source = (OUT / "unicode-search-independent.pdf").read_bytes()
(OUT / "malformed-truncated.pdf").write_bytes(source[: max(128, len(source) // 2)])
bad_xref = source.rsplit(b"startxref\n", 1)[0] + b"startxref\n999999999999\n%%EOF\n"
(OUT / "malformed-bad-startxref.pdf").write_bytes(bad_xref)

print("Run qpdf --encrypt reader owner 256 -- fixtures/unicode-search-independent.pdf "
      "fixtures/encrypted-aes256-user-reader.pdf")
print("Verify an approved corpus with: cd fixtures && sha256sum -c ../SHA256SUMS")
