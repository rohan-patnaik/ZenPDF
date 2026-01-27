FROM python:3.11-slim

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

RUN addgroup --system zenpdf && adduser --system --ingroup zenpdf zenpdf

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY zenpdf_worker ./zenpdf_worker
COPY main.py ./main.py

RUN chown -R zenpdf:zenpdf /app

USER zenpdf

CMD ["python", "main.py"]
