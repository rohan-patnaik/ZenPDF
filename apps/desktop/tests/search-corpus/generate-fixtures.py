#!/usr/bin/env python3
"""Generate and verify the source-owned ZenPDF search corpus externally."""

from __future__ import annotations

import argparse
import hashlib
import io
import subprocess
import zlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4, letter
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


FONT_PATH = "/usr/share/fonts/noto/NotoSans-Regular.ttf"
pdfmetrics.registerFont(TTFont("NotoSans", FONT_PATH))


def unicode_pdf(path: Path) -> None:
    document = canvas.Canvas(str(path), pagesize=A4, pageCompression=1, invariant=1)
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
    document = canvas.Canvas(str(path), pagesize=letter, pageCompression=1, invariant=1)
    document.drawImage(ImageReader(buffer), 36, 300, width=540, height=337.5)
    document.save()


def long_search_pdf(path: Path, pages: int = 400) -> None:
    document = canvas.Canvas(str(path), pagesize=letter, pageCompression=1, invariant=1)
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
    document = canvas.Canvas(str(path), pagesize=letter, pageCompression=1, invariant=1)
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


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_directory", help="new external directory for the corpus")
    arguments = parser.parse_args()
    output = Path(arguments.output_directory).expanduser().resolve()
    source_root = Path(__file__).resolve().parents[4]
    if output == source_root or source_root in output.parents:
        raise SystemExit(f"refusing output inside source checkout: {output}")
    if output.exists():
        raise SystemExit(f"refusing existing output directory: {output}")
    output.mkdir(parents=True, mode=0o700)

    unicode_pdf(output / "unicode-search-independent.pdf")
    image_only_pdf(output / "image-only-no-text-layer.pdf")
    long_search_pdf(output / "long-search-400-pages.pdf")
    many_results_pdf(output / "many-results-4000-matches.pdf")
    huge_page_box_pdf(output / "hostile-huge-page-box.pdf")
    flate_expansion_pdf(output / "hostile-flate-expansion-32m.pdf")

    source = (output / "unicode-search-independent.pdf").read_bytes()
    (output / "malformed-truncated.pdf").write_bytes(source[: max(128, len(source) // 2)])
    bad_xref = source.rsplit(b"startxref\n", 1)[0] + b"startxref\n999999999999\n%%EOF\n"
    (output / "malformed-bad-startxref.pdf").write_bytes(bad_xref)
    subprocess.run([
        "qpdf", "--allow-weak-crypto", "--static-id", "--static-aes-iv",
        "--encrypt", "reader", "owner", "128", "--use-aes=y", "--",
        str(output / "unicode-search-independent.pdf"),
        str(output / "encrypted-aes128-user-reader.pdf"),
    ], check=True)

    manifest = Path(__file__).with_name("SHA256SUMS")
    expected = {
        fields[1]: fields[0]
        for line in manifest.read_text(encoding="utf-8").splitlines()
        if (fields := line.split())
    }
    actual = {path.name: sha256(path) for path in output.iterdir() if path.is_file()}
    if actual != expected:
        lines = [f"{digest}  {name}" for name, digest in sorted(actual.items())]
        raise SystemExit("generated corpus does not match SHA256SUMS:\n" + "\n".join(lines))
    print(f"generated and verified approved corpus: {output}")


if __name__ == "__main__":
    main()
