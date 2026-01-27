from pathlib import Path
from tempfile import TemporaryDirectory

from unittest.mock import Mock, patch

import pytest
from docx import Document
from PIL import Image
from pypdf import PdfReader, PdfWriter

from zenpdf_worker.tools import (
    MAX_WEB_BYTES,
    html_to_pdf,
    image_to_pdf,
    merge_pdfs,
    office_to_pdf,
    pdf_to_docx,
    pdf_to_xlsx,
    pdf_to_jpg,
    rotate_pdf,
    split_pdf,
    web_to_pdf,
    zip_outputs,
)


def _make_pdf(path: Path, pages: int) -> None:
    """Create a blank PDF with the given number of pages."""
    writer = PdfWriter()
    for _ in range(pages):
        writer.add_blank_page(width=300, height=300)
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
        self._ip = ip

    def getpeername(self):
        return (self._ip, 443)


class _DummyConnection:
    """Mock connection wrapper."""
    def __init__(self, ip: str) -> None:
        self.sock = _DummySocket(ip)


class _DummyRaw:
    """Mock raw response wrapper."""
    def __init__(self, ip: str) -> None:
        self._connection = _DummyConnection(ip)


class _DummyResponse:
    """Mock response used for web-to-pdf tests."""
    def __init__(self, body: bytes, status_code: int = 200, ip: str = "93.184.216.34") -> None:
        self._body = body
        self.status_code = status_code
        self.encoding = "utf-8"
        self.raw = _DummyRaw(ip)

    def iter_content(self, chunk_size: int = 1024):
        yield self._body

    def raise_for_status(self) -> None:
        if self.status_code >= 400:
            raise RuntimeError("Bad response")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


class _DummySession:
    """Mock requests session wrapper."""
    def __init__(self, response: _DummyResponse) -> None:
        self._response = response

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def close(self):
        return None

    def mount(self, *_args, **_kwargs):
        return None

    def get(self, *_args, **_kwargs):
        return self._response
