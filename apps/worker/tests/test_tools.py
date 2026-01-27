from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image
from pypdf import PdfReader, PdfWriter

from zenpdf_worker.tools import (
    image_to_pdf,
    merge_pdfs,
    pdf_to_jpg,
    rotate_pdf,
    split_pdf,
    zip_outputs,
)


def _make_pdf(path: Path, pages: int) -> None:
    writer = PdfWriter()
    for _ in range(pages):
        writer.add_blank_page(width=300, height=300)
    with path.open("wb") as handle:
        writer.write(handle)


def test_merge_pdfs() -> None:
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first = temp_path / "first.pdf"
        second = temp_path / "second.pdf"
        _make_pdf(first, 1)
        _make_pdf(second, 2)

        output = merge_pdfs([first, second], temp_path / "merged.pdf")
        reader = PdfReader(str(output))
        assert len(reader.pages) == 3


def test_split_pdf_ranges() -> None:
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 3)

        outputs = split_pdf(source, temp_path, "1-2,3")
        assert len(outputs) == 2
        reader = PdfReader(str(outputs[0]))
        assert len(reader.pages) == 2


def test_rotate_pdf() -> None:
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)
        output = rotate_pdf(source, temp_path / "rotated.pdf", 90, None)
        assert output.exists()


def test_image_to_pdf_and_pdf_to_jpg() -> None:
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        image_path = temp_path / "sample.png"
        image = Image.new("RGB", (200, 200), color=(120, 140, 180))
        image.save(image_path)

        pdf_path = image_to_pdf([image_path], temp_path / "image.pdf")
        assert pdf_path.exists()

        images = pdf_to_jpg(pdf_path, temp_path, dpi=72)
        assert len(images) == 1
        zipped = zip_outputs(images, temp_path / "pages.zip")
        assert zipped.exists()
