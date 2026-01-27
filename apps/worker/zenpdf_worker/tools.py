from __future__ import annotations

import zipfile
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

import img2pdf
import fitz
from pypdf import PdfReader, PdfWriter


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
    """Expand a range string into an ordered page list."""
    pages: List[int] = []
    for start, end in _parse_ranges(value, total_pages):
        pages.extend(range(start, end + 1))
    return [page for page in pages if 1 <= page <= total_pages]


def _rotate_page(page, angle: int) -> None:
    """Rotate a PDF page using whichever API is available."""
    if hasattr(page, "rotate_clockwise"):
        page.rotate_clockwise(angle)
    elif hasattr(page, "rotateClockwise"):
        page.rotateClockwise(angle)
    elif hasattr(page, "rotate"):
        page.rotate(angle)


def merge_pdfs(inputs: Sequence[Path], output_path: Path) -> Path:
    """Merge multiple PDFs into a single file."""
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
    """Reorder pages based on the provided order list."""
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


def image_to_pdf(inputs: Sequence[Path], output_path: Path) -> Path:
    """Convert images to a single PDF."""
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
