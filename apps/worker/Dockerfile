FROM python:3.11-slim

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        fonts-dejavu-core \
        ghostscript \
        jbig2enc \
        libreoffice \
        mupdf-tools \
        ocrmypdf \
        poppler-utils \
        qpdf \
        tesseract-ocr \
        tesseract-ocr-eng \
        wget \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/pdfsizeopt \
    && cd /opt/pdfsizeopt \
    && wget -O pdfsizeopt_libexec_linux.tar.gz https://github.com/pts/pdfsizeopt/releases/download/2023-04-18/pdfsizeopt_libexec_linux-v9.tar.gz \
    && echo "d24676a390b8c5ea3a3edcf9af7b69a829537b101df97cc0d08a9998774b68f5  pdfsizeopt_libexec_linux.tar.gz" | sha256sum -c - \
    && tar xzvf pdfsizeopt_libexec_linux.tar.gz \
    && rm pdfsizeopt_libexec_linux.tar.gz \
    && wget -O pdfsizeopt.single https://raw.githubusercontent.com/pts/pdfsizeopt/2bab16031dad854e42c2910859564d9a962bc16c/pdfsizeopt.single \
    && echo "3007ca4d4a82d10ce0445f7e52763d28cb9f060acc9a6c5a7a67c50dcb00d87f  pdfsizeopt.single" | sha256sum -c - \
    && chmod 755 pdfsizeopt.single \
    && ln -sf /opt/pdfsizeopt/pdfsizeopt.single /usr/local/bin/pdfsizeopt

RUN addgroup --system zenpdf && adduser --system --ingroup zenpdf zenpdf

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY zenpdf_worker ./zenpdf_worker
COPY main.py ./main.py

RUN chown -R zenpdf:zenpdf /app

USER zenpdf

CMD ["python", "main.py"]
