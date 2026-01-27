"""Conversion utilities for the worker process."""

from __future__ import annotations

import ipaddress
import os
import shutil
import socket
import subprocess
import zipfile
from html.parser import HTMLParser
from io import BytesIO
from pathlib import Path
from typing import Callable, Iterable, List, Sequence, Tuple
from urllib.parse import urlparse

import img2pdf
import fitz
import requests
from docx import Document
from fpdf import FPDF
from openpyxl import Workbook
from pypdf import PdfReader, PdfWriter
from requests_toolbelt.adapters.host_header_ssl import HostHeaderSSLAdapter


def _parse_ranges(value: str, total_pages: int) -> List[Tuple[int, int]]:
    """Parse a comma-separated list of page ranges."""
    ranges: List[Tuple[int, int]] = []
    for part in value.split(","):
        cleaned = part.strip()
        if not cleaned:
            continue
        if "-" in cleaned:
            start, end = cleaned.split("-", 1)
        else:
            start, end = cleaned, cleaned
        try:
            start_i = max(1, int(start))
            end_i = min(total_pages, int(end))
        except ValueError:
            continue
        if start_i <= end_i:
            ranges.append((start_i, end_i))
    return ranges


def _parse_page_list(value: str, total_pages: int) -> List[int]:
    """
    Expand a comma-separated page range string into an ordered list of page numbers.
    
    Parameters:
        value (str): A comma-separated string of page numbers and ranges (e.g. "1,3-5,7").
        total_pages (int): Total number of pages in the document; results are clamped to 1..total_pages.
    
    Returns:
        list[int]: Ordered list of page numbers produced by expanding the ranges in `value`.
                   Each page is within 1 and `total_pages` inclusive; overlapping ranges may produce duplicates.
    """
    pages: List[int] = []
    for start, end in _parse_ranges(value, total_pages):
        pages.extend(range(start, end + 1))
    return [page for page in pages if 1 <= page <= total_pages]


def _parse_margins(value: str) -> Tuple[float, float, float, float]:
    """
    Parse a comma-separated margin string into top, right, bottom, and left values in points.
    
    The input may be a single numeric value (applied to all four margins) or four comma-separated numeric values in the order top, right, bottom, left. Whitespace around values is ignored.
    
    Parameters:
        value (str): Margin specification as "N" or "T,R,B,L".
    
    Returns:
        tuple[float, float, float, float]: (top, right, bottom, left) in points.
    
    Raises:
        ValueError: If any component is not numeric or if the input does not contain 1 or 4 values.
    """
    parts = [part.strip() for part in value.split(",") if part.strip()]
    try:
        numbers = [float(part) for part in parts]
    except ValueError as error:
        raise ValueError("Margins must be numeric") from error
    if len(numbers) == 1:
        top = right = bottom = left = numbers[0]
    elif len(numbers) == 4:
        top, right, bottom, left = numbers
    else:
        raise ValueError("Margins must have 1 or 4 values")
    return (top, right, bottom, left)


def _rotate_page(page, angle: int) -> None:
    """
    Rotate the given PDF page object by the specified angle in degrees, modifying the page in place.
    
    Parameters:
        page: PDF page object that provides a rotation method (one of `rotate_clockwise`, `rotateClockwise`, or `rotate`).
        angle (int): Rotation angle in degrees clockwise.
    """
    if hasattr(page, "rotate_clockwise"):
        page.rotate_clockwise(angle)
    elif hasattr(page, "rotateClockwise"):
        page.rotateClockwise(angle)
    elif hasattr(page, "rotate"):
        page.rotate(angle)


def _points_to_mm(points: float) -> float:
    """
    Convert a length in PDF points to millimeters.
    
    Returns:
        millimeters (float): The length converted from points to millimeters.
    """
    return points * 25.4 / 72


def _build_overlay_page(
    width_points: float,
    height_points: float,
    draw_fn: Callable[[FPDF, float, float], None],
):
    """
    Create a single-page PDF overlay sized to the given page dimensions and rendered by the provided draw callback.
    
    Parameters:
        width_points (float): Page width in PDF points.
        height_points (float): Page height in PDF points.
        draw_fn (Callable[[FPDF, float, float], None]): Callback that draws onto an FPDF instance; called with the FPDF object and the page width and height in millimeters.
    
    Returns:
        page: A single page object from a PdfReader representing the generated overlay.
    """
    width_mm = _points_to_mm(width_points)
    height_mm = _points_to_mm(height_points)
    orientation = "L" if width_mm > height_mm else "P"
    pdf = FPDF(orientation=orientation, unit="mm", format=(width_mm, height_mm))
    pdf.set_margins(0, 0, 0)
    pdf.set_auto_page_break(auto=False)
    pdf.add_page()
    draw_fn(pdf, width_mm, height_mm)
    pdf_output = pdf.output(dest="S")
    if isinstance(pdf_output, str):
        pdf_bytes = pdf_output.encode("latin-1")
    else:
        pdf_bytes = bytes(pdf_output)
    overlay_reader = PdfReader(BytesIO(pdf_bytes))
    return overlay_reader.pages[0]


def merge_pdfs(inputs: Sequence[Path], output_path: Path) -> Path:
    """
    Merge multiple PDF files into a single PDF.
    
    Parameters:
        inputs (Sequence[Path]): Paths to source PDF files, merged in the given order.
        output_path (Path): Destination path for the merged PDF.
    
    Returns:
        Path: The path to the written merged PDF (same as `output_path`).
    """
    writer = PdfWriter()
    for path in inputs:
        reader = PdfReader(str(path))
        for page in reader.pages:
            writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def split_pdf(
    input_path: Path,
    output_dir: Path,
    ranges: str | None,
) -> List[Path]:
    """Split a PDF into multiple files based on ranges."""
    reader = PdfReader(str(input_path))
    total_pages = len(reader.pages)
    output_files: List[Path] = []

    if ranges:
        page_ranges = _parse_ranges(ranges, total_pages)
        if not page_ranges:
            raise ValueError("No valid page ranges provided")
    else:
        page_ranges = [(index, index) for index in range(1, total_pages + 1)]

    for index, (start, end) in enumerate(page_ranges, start=1):
        writer = PdfWriter()
        for page_number in range(start - 1, end):
            writer.add_page(reader.pages[page_number])
        output_path = output_dir / f"split_{index}.pdf"
        with output_path.open("wb") as handle:
            writer.write(handle)
        output_files.append(output_path)
    return output_files


def compress_pdf(input_path: Path, output_path: Path) -> Path:
    """Compress a PDF by rewriting content streams."""
    reader = PdfReader(str(input_path))
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    compress = getattr(writer, "compress_content_streams", None)
    if callable(compress):
        compress()
    writer.add_metadata({})
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def rotate_pdf(
    input_path: Path,
    output_path: Path,
    angle: int,
    pages: str | None,
) -> Path:
    """Rotate selected pages by the provided angle."""
    reader = PdfReader(str(input_path))
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = set(_parse_page_list(pages, total_pages)) if pages else None
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            _rotate_page(page, angle)
        writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def remove_pages(
    input_path: Path,
    output_path: Path,
    pages: str,
) -> Path:
    """Remove pages listed in the range string."""
    reader = PdfReader(str(input_path))
    total_pages = len(reader.pages)
    remove_set = set(_parse_page_list(pages, total_pages))
    writer = PdfWriter()
    for index, page in enumerate(reader.pages, start=1):
        if index not in remove_set:
            writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def reorder_pages(
    input_path: Path,
    output_path: Path,
    order: str,
) -> Path:
    """
    Reorder pages of a PDF file according to a page-order specification.
    
    The `order` string specifies individual pages and ranges (for example, "1,3-5"); page numbers are 1-indexed. If the parsed order is empty, the original page order is preserved. The reordered PDF is written to `output_path`.
    
    Parameters:
        order (str): A comma-separated page list and ranges defining the desired page order.
    
    Returns:
        Path: The path to the written output PDF.
    """
    reader = PdfReader(str(input_path))
    total_pages = len(reader.pages)
    order_list = _parse_page_list(order, total_pages) or list(
        range(1, total_pages + 1)
    )
    writer = PdfWriter()
    for page_index in order_list:
        writer.add_page(reader.pages[page_index - 1])
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def watermark_pdf(
    input_path: Path,
    output_path: Path,
    text: str,
    pages: str | None,
) -> Path:
    """
    Apply a centered text watermark to selected pages of a PDF.
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the watermarked PDF will be written.
        text (str): Watermark text to place centered on each target page.
        pages (str | None): Page selection expressed as a range string (e.g. "1-3,5"); pages are 1-based.
            If None, the watermark is applied to every page.
    
    Returns:
        Path: The same as `output_path` after the watermarked PDF has been written.
    """
    reader = PdfReader(str(input_path))
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = set(_parse_page_list(pages, total_pages)) if pages else None
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            width = float(page.mediabox.width)
            height = float(page.mediabox.height)

            def _draw(pdf: FPDF, width_mm: float, height_mm: float) -> None:
                """
                Render centered overlay text onto the provided PDF page area.
                
                Calculates a font size from the smaller of the page width and height (clamped to the range 18-48), configures a unicode-capable font if required, sets a medium-gray text color, and writes the overlay text centered horizontally at the vertical midpoint of the page.
                
                Parameters:
                    pdf (FPDF): The FPDF instance representing the overlay page to draw on.
                    width_mm (float): Page width in millimeters.
                    height_mm (float): Page height in millimeters.
                """
                font_size = min(max(int(min(width_mm, height_mm) * 0.12), 18), 48)
                _set_overlay_font(pdf, text, font_size)
                pdf.set_text_color(160, 160, 160)
                pdf.set_xy(0, height_mm / 2)
                pdf.cell(width_mm, 10, text, align="C")

            overlay = _build_overlay_page(width, height, _draw)
            page.merge_page(overlay)
        writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def page_numbers_pdf(
    input_path: Path,
    output_path: Path,
    start: int,
    pages: str | None,
) -> Path:
    """
    Add sequential page numbers as footers to selected pages of a PDF.
    
    Parameters:
        input_path (Path): Path to the source PDF file.
        output_path (Path): Path where the resulting PDF will be written.
        start (int): Starting page number to apply to the first page (incremented per page).
        pages (str | None): Optional page selection string (e.g., "1-3,5") determining which pages receive numbers; if None, all pages are numbered.
    
    Returns:
        Path: The path to the written PDF file (same as output_path).
    """
    reader = PdfReader(str(input_path))
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = set(_parse_page_list(pages, total_pages)) if pages else None
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            width = float(page.mediabox.width)
            height = float(page.mediabox.height)
            number = start + index - 1

            def _draw(pdf: FPDF, width_mm: float, height_mm: float) -> None:
                """
                Draws a right-aligned numeric footer near the bottom edge of the overlay page.
                
                Positions and renders the page number (captured from the surrounding scope) as a footer using a font size chosen to fit the page: the size is proportional to the smaller page dimension and clamped to the range 8-16 points. The rendered text is right-aligned with a 10 mm right/bottom margin and uses a muted gray color.
                
                Parameters:
                    pdf (FPDF): The FPDF instance used to draw on the overlay page.
                    width_mm (float): Page width in millimeters.
                    height_mm (float): Page height in millimeters.
                
                Returns:
                    None
                """
                font_size = min(max(int(min(width_mm, height_mm) * 0.04), 8), 16)
                _set_overlay_font(pdf, str(number), font_size)
                pdf.set_text_color(60, 60, 60)
                margin = 10
                pdf.set_xy(0, height_mm - margin)
                pdf.cell(width_mm - margin, 6, str(number), align="R")

            overlay = _build_overlay_page(width, height, _draw)
            page.merge_page(overlay)
        writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def crop_pdf(
    input_path: Path,
    output_path: Path,
    margins: str,
    pages: str | None,
) -> Path:
    """
    Crop selected pages of a PDF by the specified margins (measured in PDF points).
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the cropped PDF will be written.
        margins (str): Margin specification in points; either a single numeric value applied to all sides or four comma-separated values in the order top,right,bottom,left.
        pages (str | None): Optional page selection string (e.g., "1-3,5") specifying which pages to crop; when None, all pages are processed.
    
    Returns:
        Path: The path to the written cropped PDF.
    
    Raises:
        ValueError: If the provided margins would remove an entire page or if margin parsing fails.
    """
    reader = PdfReader(str(input_path))
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = set(_parse_page_list(pages, total_pages)) if pages else None
    top, right, bottom, left = _parse_margins(margins)
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            lower_left_x = float(page.mediabox.lower_left[0]) + left
            lower_left_y = float(page.mediabox.lower_left[1]) + bottom
            upper_right_x = float(page.mediabox.upper_right[0]) - right
            upper_right_y = float(page.mediabox.upper_right[1]) - top
            if upper_right_x <= lower_left_x or upper_right_y <= lower_left_y:
                raise ValueError("Crop margins remove the entire page")
            page.mediabox.lower_left = (lower_left_x, lower_left_y)
            page.mediabox.upper_right = (upper_right_x, upper_right_y)
            page.cropbox.lower_left = (lower_left_x, lower_left_y)
            page.cropbox.upper_right = (upper_right_x, upper_right_y)
            page.trimbox.lower_left = (lower_left_x, lower_left_y)
            page.trimbox.upper_right = (upper_right_x, upper_right_y)
        writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def unlock_pdf(input_path: Path, output_path: Path, password: str) -> Path:
    """
    Remove password protection from the PDF at input_path and write the unlocked PDF to output_path.
    
    Preserves document pages and metadata. Raises ValueError if the PDF is encrypted and the provided password fails to decrypt it.
    
    Parameters:
        input_path (Path): Path to the source PDF (may be encrypted).
        output_path (Path): Path where the unlocked PDF will be written.
        password (str): Password to use for decryption.
    
    Returns:
        output_path (Path): The path to the written unlocked PDF.
    
    Raises:
        ValueError: If the PDF is encrypted and the provided password does not unlock it.
    """
    reader = PdfReader(str(input_path))
    if reader.is_encrypted:
        result = reader.decrypt(password)
        if result == 0:
            raise ValueError("Unable to unlock PDF")
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    writer.add_metadata(reader.metadata or {})
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def protect_pdf(input_path: Path, output_path: Path, password: str) -> Path:
    """
    Encrypts an unencrypted PDF file with the specified password.
    
    Sets the same value as both the user and owner password and enables 128-bit encryption; preserves the input PDF's metadata.
    
    Parameters:
        input_path (Path): Path to the source PDF file (must be unencrypted).
        output_path (Path): Path where the encrypted PDF will be written.
        password (str): Password to apply as both the user and owner password.
    
    Returns:
        Path: The path to the written encrypted PDF (the provided output_path).
    
    Raises:
        ValueError: If the input PDF is already encrypted.
    """
    reader = PdfReader(str(input_path))
    if reader.is_encrypted:
        raise ValueError("PDF is already encrypted")
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    writer.add_metadata(reader.metadata or {})
    writer.encrypt(user_password=password, owner_password=password, use_128bit=True)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def redact_pdf(
    input_path: Path,
    output_path: Path,
    text: str,
    pages: str | None,
) -> Path:
    """
    Redact all occurrences of a given text in a PDF, optionally restricted to specific pages.
    
    Searches each targeted page for exact occurrences of `text`, adds black redact annotations over matches, applies the redactions, and writes the modified PDF to `output_path`.
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the redacted PDF will be written.
        text (str): Text to search for and redact; matches are searched as exact occurrences.
        pages (str | None): Optional page selection string (e.g., "1-3,5") restricting which pages to process; if `None`, all pages are searched.
    
    Returns:
        Path: The `output_path` of the saved redacted PDF.
    """
    document = fitz.open(str(input_path))
    total_pages = document.page_count
    target_pages = set(_parse_page_list(pages, total_pages)) if pages else None
    for index in range(total_pages):
        page_number = index + 1
        if target_pages is not None and page_number not in target_pages:
            continue
        page = document.load_page(index)
        rectangles = page.search_for(text)
        if not rectangles:
            continue
        for rect in rectangles:
            page.add_redact_annot(rect, fill=(0, 0, 0))
        page.apply_redactions()
    document.save(str(output_path), deflate=True)
    document.close()
    return output_path


def compare_pdfs(first_path: Path, second_path: Path, output_path: Path) -> Path:
    """
    Produce a plain-text comparison report summarizing page counts and per-page text differences between two PDFs.
    
    The report includes the input filenames, their page counts, and entries for pages that are missing from one file or whose extracted text differs; if no differences are found the report records that fact. The report is written to output_path using UTF-8.
    
    Parameters:
        first_path (Path): Path to the first PDF file (File A).
        second_path (Path): Path to the second PDF file (File B).
        output_path (Path): Destination path for the generated text report.
    
    Returns:
        Path: The path to the written report (output_path).
    """
    reader_a = PdfReader(str(first_path))
    reader_b = PdfReader(str(second_path))
    pages_a = len(reader_a.pages)
    pages_b = len(reader_b.pages)
    lines = [
        "ZenPDF comparison report",
        f"File A: {first_path.name}",
        f"File B: {second_path.name}",
        f"Pages: {pages_a} vs {pages_b}",
        "",
    ]
    differences: List[str] = []
    if pages_a != pages_b:
        differences.append("Page counts differ.")
    for index in range(max(pages_a, pages_b)):
        if index >= pages_a:
            differences.append(f"Page {index + 1}: missing from file A")
            continue
        if index >= pages_b:
            differences.append(f"Page {index + 1}: missing from file B")
            continue
        text_a = (reader_a.pages[index].extract_text() or "").strip()
        text_b = (reader_b.pages[index].extract_text() or "").strip()
        if text_a != text_b:
            differences.append(f"Page {index + 1}: text differs")
    if not differences:
        lines.append("No text differences detected.")
    else:
        lines.extend(differences)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    return output_path


def image_to_pdf(inputs: Sequence[Path], output_path: Path) -> Path:
    """
    Combine one or more image files into a single PDF file.
    
    Parameters:
        inputs (Sequence[Path]): Paths to image files to include, in order.
        output_path (Path): Destination path for the generated PDF.
    
    Returns:
        Path: The path to the written PDF (same object as `output_path`).
    
    Raises:
        ValueError: If image rendering to PDF fails.
    """
    pdf_bytes = img2pdf.convert([str(path) for path in inputs])
    if pdf_bytes is None:
        raise ValueError("Failed to render images to PDF")
    output_path.write_bytes(pdf_bytes)
    return output_path


def pdf_to_jpg(input_path: Path, output_dir: Path, dpi: int = 150) -> List[Path]:
    """Render each PDF page to a JPG image."""
    document = fitz.open(str(input_path))
    scale = dpi / 72
    matrix = fitz.Matrix(scale, scale)
    outputs: List[Path] = []
    for index in range(document.page_count):
        page = document.load_page(index)
        render = getattr(page, "get_pixmap", None)
        if not callable(render):
            raise ValueError("PDF renderer unavailable")
        pix = render(matrix=matrix)
        output_path = output_dir / f"page_{index + 1}.jpg"
        saver = getattr(pix, "save", None)
        if not callable(saver):
            raise ValueError("Rendered page cannot be saved")
        saver(str(output_path))
        outputs.append(output_path)
    document.close()
    return outputs


def zip_outputs(outputs: Iterable[Path], zip_path: Path) -> Path:
    """Zip multiple output files into a single archive."""
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in outputs:
            archive.write(item, arcname=item.name)
    return zip_path


MAX_WEB_BYTES = 2 * 1024 * 1024
UNICODE_FONT_PATHS = (
    Path(__file__).resolve().parent / "assets" / "DejaVuSans.ttf",
    Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
    Path("/usr/share/fonts/DejaVuSans.ttf"),
)


def _resolve_unicode_font_path() -> Path | None:
    """
    Locate a Unicode-compatible TrueType font file if one is available.
    
    Checks the ZENPDF_TTF_PATH environment variable first, then falls back to known candidate paths.
    
    Returns:
        Path | None: Path to the font file if found, `None` otherwise.
    """
    env_path = os.getenv("ZENPDF_TTF_PATH")
    if env_path:
        candidate = Path(env_path)
        if candidate.is_file():
            return candidate
    for candidate in UNICODE_FONT_PATHS:
        if candidate.is_file():
            return candidate
    return None


def _set_overlay_font(pdf: FPDF, text: str, size: int) -> None:
    """
    Selects and configures an appropriate font on the given FPDF instance for rendering overlay text.
    
    Attempts to load a Unicode-capable DejaVu Sans from the environment or known paths; if unavailable, verifies whether the provided text can be encoded in Latin-1 and falls back to Helvetica. If the text requires Unicode and no Unicode font is available, raises RuntimeError.
    
    Parameters:
        pdf (FPDF): The FPDF instance to configure.
        text (str): Sample text to test whether a Unicode font is required.
        size (int): Font size to set on the PDF.
    
    Raises:
        RuntimeError: If the text contains characters that require a Unicode font but no Unicode font path is available.
    """
    font_path = _resolve_unicode_font_path()
    if font_path:
        pdf.add_font("DejaVuSans", fname=str(font_path), uni=True)
        pdf.set_font("DejaVuSans", size=size)
        return
    try:
        text.encode("latin-1")
    except UnicodeEncodeError as error:
        raise RuntimeError(
            "Unicode font unavailable. Set ZENPDF_TTF_PATH to a DejaVuSans.ttf path."
        ) from error
    pdf.set_font("Helvetica", size=size)


class _HTMLTextExtractor(HTMLParser):
    """Minimal HTML to text extractor."""

    def __init__(self) -> None:
        """Initialize the parser."""
        super().__init__()
        self._parts: List[str] = []

    def handle_data(self, data: str) -> None:
        """Capture text content from HTML."""
        text = data.strip()
        if text:
            self._parts.append(text)

    def text(self) -> str:
        """Return the collected text content."""
        return "\n".join(self._parts)


def html_to_pdf(html: str, output_path: Path) -> Path:
    """Render basic HTML text into a PDF."""
    parser = _HTMLTextExtractor()
    parser.feed(html)
    text = parser.text() or "(no content)"

    pdf = FPDF(orientation="P", unit="mm", format="A4")
    pdf.set_margins(15, 15, 15)
    pdf.set_auto_page_break(auto=True, margin=15)
    pdf.add_page()
    font_path = _resolve_unicode_font_path()
    if font_path:
        pdf.add_font("DejaVuSans", fname=str(font_path), uni=True)
        pdf.set_font("DejaVuSans", size=12)
    else:
        try:
            text.encode("latin-1")
        except UnicodeEncodeError as error:
            raise RuntimeError(
                "Unicode font unavailable. Set ZENPDF_TTF_PATH to a DejaVuSans.ttf path."
            ) from error
        pdf.set_font("Helvetica", size=12)
    max_width = pdf.w - pdf.l_margin - pdf.r_margin
    for line in text.splitlines():
        if line.strip():
            pdf.multi_cell(max_width, 6, line)
        else:
            pdf.ln(5)
    pdf.output(str(output_path))
    return output_path


def web_to_pdf(url: str, output_path: Path) -> Path:
    """Fetch a URL and convert its HTML to PDF."""
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError("Only http/https URLs are supported")
    target_ip = _resolve_public_ip(parsed.hostname)
    is_ipv6 = isinstance(ipaddress.ip_address(target_ip), ipaddress.IPv6Address)
    host = f"[{target_ip}]" if is_ipv6 else target_ip
    netloc = f"{host}:{parsed.port}" if parsed.port is not None else host
    try:
        hostname_ip = ipaddress.ip_address(parsed.hostname)
        hostname_is_ipv6 = isinstance(hostname_ip, ipaddress.IPv6Address)
    except ValueError:
        hostname_is_ipv6 = False

    host_header = (
        f"[{parsed.hostname}]" if hostname_is_ipv6 else parsed.hostname
    )
    if parsed.port is not None:
        host_header = f"{host_header}:{parsed.port}"
    target_url = parsed._replace(netloc=netloc).geturl()

    body = bytearray()
    with requests.Session() as session:
        if parsed.scheme == "https":
            session.mount("https://", HostHeaderSSLAdapter())

        with session.get(
            target_url,
            timeout=20,
            allow_redirects=False,
            stream=True,
            headers={"Host": host_header},
        ) as response:
            if 300 <= response.status_code < 400:
                raise ValueError("Redirects are not allowed")
            response.raise_for_status()
            for chunk in response.iter_content(chunk_size=64 * 1024):
                if not chunk:
                    continue
                body.extend(chunk)
                if len(body) > MAX_WEB_BYTES:
                    raise ValueError("Web response too large")
            encoding = response.encoding or "utf-8"

    html = body.decode(encoding, errors="replace")
    return html_to_pdf(html, output_path)


def office_to_pdf(input_path: Path, output_dir: Path) -> Path:
    """Convert an Office document to PDF using LibreOffice."""
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        raise RuntimeError("LibreOffice is required for office-to-pdf")

    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        result = subprocess.run(
            [
                soffice,
                "--headless",
                "--convert-to",
                "pdf",
                "--outdir",
                str(output_dir),
                str(input_path),
            ],
            capture_output=True,
            text=True,
            check=False,
            timeout=120,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Office conversion timed out") from error
    if result.returncode != 0:
        raise RuntimeError(result.stderr or result.stdout or "Office conversion failed")

    output_path = output_dir / f"{input_path.stem}.pdf"
    if not output_path.exists():
        raise RuntimeError("Office conversion produced no output")
    return output_path


def pdf_to_docx(input_path: Path, output_path: Path) -> Path:
    """Convert a PDF into a Word document by extracting text."""
    reader = PdfReader(str(input_path))
    document = Document()
    for index, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        if index > 1:
            document.add_page_break()
        if text.strip():
            for line in text.splitlines():
                if line.strip():
                    document.add_paragraph(line)
        else:
            document.add_paragraph("")
    document.save(str(output_path))
    return output_path


def pdf_to_xlsx(input_path: Path, output_path: Path) -> Path:
    """Convert a PDF into an Excel workbook by extracting text."""
    reader = PdfReader(str(input_path))
    workbook = Workbook()
    sheet = workbook.active
    row = 1
    for index, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        lines = [line for line in text.splitlines() if line.strip()]
        if not lines:
            sheet.cell(row=row, column=1, value=f"Page {index}")
            row += 1
        else:
            for line in lines:
                sheet.cell(row=row, column=1, value=line)
                row += 1
        row += 1
    workbook.save(str(output_path))
    return output_path


def _resolve_public_ip(hostname: str) -> str:
    """Resolve a hostname to a public IP address."""
    try:
        infos = socket.getaddrinfo(hostname, None)
    except socket.gaierror as error:
        raise ValueError("Unable to resolve host") from error

    for info in infos:
        address = str(info[4][0])
        if _is_public_ip(address):
            return address
    raise ValueError("URL host is not allowed")


def _is_public_ip(address: str) -> bool:
    """Return True if the address is a public IP."""
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return not (
        ip.is_private
        or ip.is_loopback
        or ip.is_link_local
        or ip.is_multicast
        or ip.is_reserved
        or ip.is_unspecified
    )
