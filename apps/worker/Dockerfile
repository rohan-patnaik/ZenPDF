FROM python:3.11.13-slim-bookworm@sha256:86adf8dbadc3d6e82ee5dd2c74bec2e1c2467cdad47886280501df722372d2e1

WORKDIR /app

COPY apt-packages.lock /tmp/zenpdf-apt-packages.lock
RUN printf '%s\n' \
        'deb [check-valid-until=no] https://snapshot.debian.org/archive/debian/20260817T000000Z bookworm main' \
        'deb [check-valid-until=no] https://snapshot.debian.org/archive/debian/20260817T000000Z bookworm-updates main' \
        'deb [check-valid-until=no] https://snapshot.debian.org/archive/debian-security/20260817T000000Z bookworm-security main' \
        > /etc/apt/sources.list \
    && rm -f /etc/apt/sources.list.d/debian.sources \
    && apt-get -o Acquire::Check-Valid-Until=false -o Acquire::Retries=5 update \
    && xargs -r apt-get -o Acquire::Retries=5 install -y --no-install-recommends < /tmp/zenpdf-apt-packages.lock \
    && rm /tmp/zenpdf-apt-packages.lock \
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

COPY requirements-ci.lock ./requirements.lock
RUN pip install --no-cache-dir --require-hashes -r requirements.lock \
    && rm requirements.lock

COPY zenpdf_worker ./zenpdf_worker
COPY main.py ./main.py

RUN mkdir -p /var/lib/zenpdf-worker/upload-recovery \
    && chown -R zenpdf:zenpdf /app /var/lib/zenpdf-worker

USER zenpdf

CMD ["python", "main.py"]
