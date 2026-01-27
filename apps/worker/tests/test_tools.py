"""Tests for worker conversion utilities."""

from pathlib import Path
from tempfile import TemporaryDirectory

import subprocess
from unittest.mock import patch

import pytest
import fitz
from docx import Document
from fpdf import FPDF
from openpyxl import load_workbook
from PIL import Image
from pypdf import PdfReader, PdfWriter

from zenpdf_worker.tools import (
    MAX_WEB_BYTES,
    compare_pdfs,
    crop_pdf,
    highlight_pdf,
    html_to_pdf,
    image_to_pdf,
    merge_pdfs,
    office_to_pdf,
    page_numbers_pdf,
    pdf_to_pdfa,
    pdf_to_docx,
    pdf_to_docx_ocr,
    pdf_to_text,
    pdf_to_xlsx,
    pdf_to_xlsx_ocr,
    pdf_to_jpg,
    protect_pdf,
    repair_pdf,
    redact_pdf,
    rotate_pdf,
    split_pdf,
    unlock_pdf,
    watermark_pdf,
    web_to_pdf,
    zip_outputs,
)


def _make_pdf(path: Path, pages: int) -> None:
    """
    Create a blank PDF at the given path containing the specified number of pages.
    
    Parameters:
        path (Path): Destination file path for the generated PDF.
        pages (int): Number of blank pages to include (must be greater than or equal to 0).
    """
    writer = PdfWriter()
    for _ in range(pages):
        writer.add_blank_page(width=300, height=300)
    with path.open("wb") as handle:
        writer.write(handle)


def _make_text_pdf(path: Path, text: str) -> None:
    """Create a single-page PDF with text content."""
    pdf = FPDF(orientation="P", unit="mm", format="A4")
    pdf.add_page()
    pdf.set_font("Helvetica", size=14)
    pdf.text(10, 20, text)
    pdf.output(str(path))


def _make_pdf_with_metadata(path: Path, title: str) -> None:
    """Create a PDF with a Title metadata value."""
    writer = PdfWriter()
    writer.add_blank_page(width=300, height=300)
    writer.add_metadata({"/Title": title})
    with path.open("wb") as handle:
        writer.write(handle)


def test_merge_pdfs() -> None:
    """Merge multiple PDFs into one output."""
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
    """Split a PDF using page ranges."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 3)

        outputs = split_pdf(source, temp_path, "1-2,3")
        assert len(outputs) == 2
        reader = PdfReader(str(outputs[0]))
        assert len(reader.pages) == 2


def test_rotate_pdf() -> None:
    """Rotate pages in a PDF."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)
        output = rotate_pdf(source, temp_path / "rotated.pdf", 90, None)
        assert output.exists()


def test_watermark_and_page_numbers() -> None:
    """Apply watermark and page numbers to a PDF."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 2)

        watermarked = watermark_pdf(source, temp_path / "watermarked.pdf", "CONFIDENTIAL", None)
        numbered = page_numbers_pdf(source, temp_path / "numbered.pdf", 3, None)

        watermarked_reader = PdfReader(str(watermarked))
        numbered_reader = PdfReader(str(numbered))
        assert "CONFIDENTIAL" in (watermarked_reader.pages[0].extract_text() or "")
        assert "3" in (numbered_reader.pages[0].extract_text() or "")


def test_watermark_rejects_invalid_pages() -> None:
    """Reject invalid page selections when watermarking."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 2)

        with pytest.raises(ValueError):
            watermark_pdf(source, temp_path / "watermarked.pdf", "NOTE", "nope")


def test_crop_pdf() -> None:
    """Crop a PDF using point margins."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)

        cropped = crop_pdf(source, temp_path / "cropped.pdf", "10,10,10,10", None)
        reader = PdfReader(str(cropped))
        page = reader.pages[0]
        assert float(page.cropbox.width) == pytest.approx(280)
        assert float(page.cropbox.height) == pytest.approx(280)


def test_crop_pdf_rejects_invalid_margins() -> None:
    """Reject invalid margin specifications when cropping."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)

        for index, margins in enumerate(["bad", "10,10", "-5"], start=1):
            with pytest.raises(ValueError):
                crop_pdf(
                    source,
                    temp_path / f"invalid_{index}.pdf",
                    margins,
                    None,
                )


def test_image_to_pdf_and_pdf_to_jpg() -> None:
    """Convert image to PDF and PDF to JPG."""
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


def test_unlock_and_protect_pdf() -> None:
    """Protect a PDF with a password and unlock it."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)

        protected = protect_pdf(source, temp_path / "protected.pdf", "secret")
        protected_reader = PdfReader(str(protected))
        assert protected_reader.is_encrypted

        unlocked = unlock_pdf(protected, temp_path / "unlocked.pdf", "secret")
        unlocked_reader = PdfReader(str(unlocked))
        assert not unlocked_reader.is_encrypted


def test_repair_pdf() -> None:
    """Rewrite a PDF into a repaired copy."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 2)

        repaired = repair_pdf(source, temp_path / "repaired.pdf")
        reader = PdfReader(str(repaired))
        assert len(reader.pages) == 2


def test_watermark_preserves_metadata() -> None:
    """Keep PDF metadata after watermarking, numbering, and rotating."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf_with_metadata(source, "ZenPDF")

        watermarked = watermark_pdf(source, temp_path / "watermarked.pdf", "NOTE", None)
        numbered = page_numbers_pdf(source, temp_path / "numbered.pdf", 1, None)
        rotated = rotate_pdf(source, temp_path / "rotated.pdf", 90, None)

        watermarked_reader = PdfReader(str(watermarked))
        numbered_reader = PdfReader(str(numbered))
        rotated_reader = PdfReader(str(rotated))
        watermarked_metadata = watermarked_reader.metadata or {}
        numbered_metadata = numbered_reader.metadata or {}
        rotated_metadata = rotated_reader.metadata or {}
        assert watermarked_metadata.get("/Title") == "ZenPDF"
        assert numbered_metadata.get("/Title") == "ZenPDF"
        assert rotated_metadata.get("/Title") == "ZenPDF"


def test_redact_pdf() -> None:
    """Redact matching text in a PDF."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_text_pdf(source, "CONFIDENTIAL")

        redacted = redact_pdf(source, temp_path / "redacted.pdf", "CONFIDENTIAL", None)
        reader = PdfReader(str(redacted))
        assert "CONFIDENTIAL" not in (reader.pages[0].extract_text() or "")


def test_highlight_pdf() -> None:
    """Highlight matching text in a PDF."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_text_pdf(source, "CONFIDENTIAL")

        highlighted = highlight_pdf(source, temp_path / "highlighted.pdf", "CONFIDENTIAL", None)
        document = fitz.open(str(highlighted))
        page = document.load_page(0)
        annotations = list(page.annots() or [])
        document.close()
        assert annotations


def test_compare_pdfs() -> None:
    """Generate a comparison report for two PDFs."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first = temp_path / "first.pdf"
        second = temp_path / "second.pdf"
        _make_text_pdf(first, "Alpha")
        _make_text_pdf(second, "Beta")

        report = compare_pdfs(first, second, temp_path / "report.txt")
        report_text = report.read_text(encoding="utf-8")
        assert "text differs" in report_text


def test_pdf_to_text() -> None:
    """Extract PDF text into a TXT file."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_text_pdf(source, "Hello")

        output = pdf_to_text(source, temp_path / "output.txt")
        assert "Hello" in output.read_text(encoding="utf-8")


def test_html_to_pdf() -> None:
    """Render HTML content into a PDF."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        output = html_to_pdf("<h1>Hello</h1><p>ZenPDF</p>", temp_path / "web.pdf")
        assert output.exists()
        assert output.stat().st_size > 0


def test_web_to_pdf_fetches_html() -> None:
    """Fetch HTML over HTTP and render to PDF."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        response = _DummyResponse(b"<p>Example</p>")
        session = _DummySession(response)
        with patch("zenpdf_worker.tools.requests.Session", return_value=session), patch(
            "zenpdf_worker.tools._resolve_public_ip", return_value="93.184.216.34"
        ):
            output = web_to_pdf("https://example.com", temp_path / "site.pdf")
        assert output.exists()


def test_web_to_pdf_blocks_private_host() -> None:
    """Reject private hosts for web-to-pdf."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        with patch(
            "zenpdf_worker.tools._resolve_public_ip",
            side_effect=ValueError("URL host is not allowed"),
        ):
            with pytest.raises(ValueError):
                web_to_pdf("http://127.0.0.1", temp_path / "blocked.pdf")


def test_web_to_pdf_blocks_redirects() -> None:
    """Reject redirect responses for web-to-pdf."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        response = _DummyResponse(b"", status_code=302)
        session = _DummySession(response)
        with patch("zenpdf_worker.tools.requests.Session", return_value=session), patch(
            "zenpdf_worker.tools._resolve_public_ip", return_value="93.184.216.34"
        ):
            with pytest.raises(ValueError):
                web_to_pdf("https://example.com", temp_path / "redirect.pdf")


def test_web_to_pdf_limits_body_size() -> None:
    """Enforce the max response size for web-to-pdf."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        response = _DummyResponse(b"a" * (MAX_WEB_BYTES + 1))
        session = _DummySession(response)
        with patch("zenpdf_worker.tools.requests.Session", return_value=session), patch(
            "zenpdf_worker.tools._resolve_public_ip", return_value="93.184.216.34"
        ):
            with pytest.raises(ValueError):
                web_to_pdf("https://example.com", temp_path / "large.pdf")


def test_pdf_to_docx_and_xlsx() -> None:
    """Convert PDF text to DOCX and XLSX files."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)

        docx_path = pdf_to_docx(source, temp_path / "output.docx")
        xlsx_path = pdf_to_xlsx(source, temp_path / "output.xlsx")

        assert docx_path.exists()
        assert xlsx_path.exists()


def test_pdf_to_docx_and_xlsx_ocr() -> None:
    """Convert a PDF to DOCX/XLSX using OCR."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 2)

        with patch(
            "zenpdf_worker.tools._ocr_image",
            side_effect=["First page", "Second page"],
        ):
            docx_path = pdf_to_docx_ocr(source, temp_path / "ocr.docx")

        with patch(
            "zenpdf_worker.tools._ocr_image",
            side_effect=["First sheet", "Second sheet"],
        ):
            xlsx_path = pdf_to_xlsx_ocr(source, temp_path / "ocr.xlsx")

        document = Document(str(docx_path))
        doc_text = "\n".join(paragraph.text for paragraph in document.paragraphs)
        assert "First page" in doc_text
        assert "Second page" in doc_text

        workbook = load_workbook(xlsx_path)
        sheet = workbook.active
        values = [cell.value for cell in sheet["A"] if cell.value]
        assert "First sheet" in values
        assert "Second sheet" in values


def test_pdfa_requires_ghostscript(monkeypatch: pytest.MonkeyPatch) -> None:
    """Fail when Ghostscript is not available for PDF/A conversion."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        _make_pdf(source, 1)

        monkeypatch.setattr("zenpdf_worker.tools.shutil.which", lambda _: None)
        with pytest.raises(RuntimeError):
            pdf_to_pdfa(source, temp_path / "output.pdf")


def test_pdfa_conversion_runs_ghostscript(monkeypatch: pytest.MonkeyPatch) -> None:
    """Run PDF/A conversion through Ghostscript."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "source.pdf"
        output = temp_path / "output.pdf"
        _make_pdf(source, 1)

        monkeypatch.setattr("zenpdf_worker.tools.shutil.which", lambda _: "/usr/bin/gs")

        def fake_run(command, **_kwargs):
            if "--version" in command:
                return subprocess.CompletedProcess(command, 0, stdout="10.03.1", stderr="")
            output.write_bytes(b"%PDF-1.7\n")
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

        monkeypatch.setattr("zenpdf_worker.tools.subprocess.run", fake_run)

        result = pdf_to_pdfa(source, output)
        assert result.exists()


def test_pdfa_rejects_encrypted_pdf() -> None:
    """Reject encrypted PDFs before invoking Ghostscript."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        source = temp_path / "encrypted.pdf"
        writer = PdfWriter()
        writer.add_blank_page(width=300, height=300)
        writer.encrypt("secret")
        with source.open("wb") as handle:
            writer.write(handle)

        with pytest.raises(ValueError):
            pdf_to_pdfa(source, temp_path / "output.pdf")


def test_office_to_pdf_missing_soffice(monkeypatch: pytest.MonkeyPatch) -> None:
    """Fail when LibreOffice is not available."""
    with TemporaryDirectory() as temp:
        temp_path = Path(temp)
        doc_path = temp_path / "sample.docx"
        document = Document()
        document.add_paragraph("Hello")
        document.save(doc_path)

        monkeypatch.setattr("zenpdf_worker.tools.shutil.which", lambda _: None)
        with pytest.raises(RuntimeError):
            office_to_pdf(doc_path, temp_path)


class _DummySocket:
    """Mock socket for response peer IP retrieval."""
    def __init__(self, ip: str) -> None:
        """Initialize the mock socket with an IP."""
        self._ip = ip

    def getpeername(self):
        """Return the mock socket peer tuple."""
        return (self._ip, 443)


class _DummyConnection:
    """Mock connection wrapper."""
    def __init__(self, ip: str) -> None:
        """Initialize the dummy connection with the socket."""
        self.sock = _DummySocket(ip)


class _DummyRaw:
    """Mock raw response wrapper."""
    def __init__(self, ip: str) -> None:
        """Initialize the raw wrapper with a connection."""
        self._connection = _DummyConnection(ip)


class _DummyResponse:
    """Mock response used for web-to-pdf tests."""
    def __init__(self, body: bytes, status_code: int = 200, ip: str = "93.184.216.34") -> None:
        """Initialize the dummy response payload."""
        self._body = body
        self.status_code = status_code
        self.encoding = "utf-8"
        self.raw = _DummyRaw(ip)

    def iter_content(self, _chunk_size: int = 1024, **_kwargs):
        """Yield the body payload as a single chunk."""
        yield self._body

    def raise_for_status(self) -> None:
        """Raise when the status indicates an error."""
        if self.status_code >= 400:
            raise RuntimeError("Bad response")

    def __enter__(self):
        """Enter the response context manager."""
        return self

    def __exit__(self, exc_type, exc, tb):
        """Exit the response context manager."""
        return False


class _DummySession:
    """Mock requests session wrapper."""
    def __init__(self, response: _DummyResponse) -> None:
        """Initialize the dummy session."""
        self._response = response

    def __enter__(self):
        """Enter the session context manager."""
        return self

    def __exit__(self, exc_type, exc, tb):
        """Exit the session context manager."""
        return False

    def close(self):
        """Close the dummy session."""
        return None

    def mount(self, *_args, **_kwargs):
        """Ignore adapter mounting for the mock session."""
        return None

    def get(self, *_args, **_kwargs):
        """Return the prepared response."""
        return self._response
