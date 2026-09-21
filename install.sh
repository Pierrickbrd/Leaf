#!/usr/bin/env bash
set -Eeuo pipefail

LEAF_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

bootstrap_from_github() {
    [[ -f "$LEAF_ROOT/VERSION" && -d "$LEAF_ROOT/server" && -d "$LEAF_ROOT/desktop" ]] && return 0
    command -v curl >/dev/null || { printf 'Leaf: curl est nécessaire pour télécharger les sources.\n' >&2; exit 1; }
    command -v tar >/dev/null || { printf 'Leaf: tar est nécessaire pour extraire les sources.\n' >&2; exit 1; }

    local ref=${LEAF_INSTALL_REF:-main} bootstrap_tmp
    [[ "$ref" =~ ^[A-Za-z0-9._/-]+$ ]] || { printf 'Leaf: LEAF_INSTALL_REF contient des caractères non valides.\n' >&2; exit 1; }
    bootstrap_tmp=$(mktemp -d /tmp/leaf-bootstrap.XXXXXX)
    trap 'rm -r -- "$bootstrap_tmp"' EXIT

    printf 'Téléchargement des sources de Leaf (%s)…\n' "$ref"
    curl --proto '=https' --tlsv1.2 -fsSL \
        "https://codeload.github.com/Pierrickbrd/Leaf/tar.gz/$ref" \
        -o "$bootstrap_tmp/leaf.tar.gz"
    tar -xzf "$bootstrap_tmp/leaf.tar.gz" --strip-components=1 -C "$bootstrap_tmp"

    local args=("$@")
    if [[ ${args[0]:-} == "update" ]]; then
        args=("${args[@]:1}")
    fi
    export LEAF_BOOTSTRAP_DIR=$bootstrap_tmp
    exec "$bootstrap_tmp/install.sh" "${args[@]}"
}
bootstrap_from_github "$@"

LEAF_VERSION=$(tr -d '[:space:]' < "$LEAF_ROOT/VERSION")
LEAF_TMP=$(mktemp -d /tmp/leaf-install.XXXXXX)
cleanup() {
    rm -r -- "$LEAF_TMP"
    if [[ ${LEAF_BOOTSTRAP_DIR:-} == /tmp/leaf-bootstrap.* && -d ${LEAF_BOOTSTRAP_DIR:-} ]]; then
        rm -r -- "$LEAF_BOOTSTRAP_DIR"
    fi
}
trap cleanup EXIT

component=""
library=""
host=""
port=""
address=""
client_key=""
key_file=""
install_user="${LEAF_INSTALL_USER:-${SUDO_USER:-$(id -un)}}"
with_dependencies=1
updating=0

usage() {
    cat <<'EOF'
Usage:
  ./install.sh server [options]
  ./install.sh client [options]
  ./install.sh all [options]
  ./install.sh update server|client|all [options]
  ./install.sh version

Commands:
  server   build and install the server and its systemd service
  client   build and install the Ubuntu client, icon and launcher
  all      install both and connect the client to the local server
  update   download the current release, then reinstall the selected component

Options:
  --library PATH   library directory (default: /srv/leaf/library)
  --host ADDRESS   address the server binds (default: 127.0.0.1)
  --port PORT      server port (default: 8081)
  --address URL    URL written into the client configuration
  --key-file PATH  read the client key from this file
  --user USER      account that runs the server and owns the client configuration
  --no-deps        do not install build dependencies with apt

Examples:
  ./install.sh server
  ./install.sh server --host 100.64.0.10
  ./install.sh client
  ./install.sh all
  leaf-install update server
EOF
}

fail() {
    printf 'Leaf: %s\n' "$*" >&2
    exit 1
}

as_root() {
    if (( EUID == 0 )); then
        "$@"
    else
        sudo "$@"
    fi
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || fail "$1 attend une valeur."
}

if [[ ${1:-} == "update" ]]; then
    updating=1
    component=${2:-}
    shift 2 || true
elif [[ ${1:-} == "version" ]]; then
    printf 'Leaf %s\n' "$LEAF_VERSION"
    exit 0
elif [[ ${1:-} == "server" || ${1:-} == "client" || ${1:-} == "all" ]]; then
    component=$1
    shift
elif [[ ${1:-} == "--help" || ${1:-} == "-h" || -z ${1:-} ]]; then
    usage
    exit 0
else
    fail "commande inconnue '${1:-}'. Lancez ./install.sh --help."
fi

[[ "$component" == "server" || "$component" == "client" || "$component" == "all" ]] \
    || fail "update attend server, client ou all."

while (($#)); do
    case "$1" in
        --library)
            need_value "$@"; library=$2; shift 2 ;;
        --host)
            need_value "$@"; host=$2; shift 2 ;;
        --port)
            need_value "$@"; port=$2; shift 2 ;;
        --address)
            need_value "$@"; address=$2; shift 2 ;;
        --key-file)
            need_value "$@"; key_file=$2; shift 2 ;;
        --user)
            need_value "$@"; install_user=$2; shift 2 ;;
        --no-deps)
            with_dependencies=0; shift ;;
        --help|-h)
            usage; exit 0 ;;
        *)
            fail "option inconnue '$1'. Lancez ./install.sh --help." ;;
    esac
done

getent passwd "$install_user" >/dev/null || fail "l’utilisateur '$install_user' n’existe pas."
install_group=$(id -gn "$install_user")
install_home=$(getent passwd "$install_user" | cut -d: -f6)

if (( updating )); then
    command -v git >/dev/null || fail "git est nécessaire pour mettre à jour cette installation source."
    [[ -z $(git -C "$LEAF_ROOT" status --porcelain) ]] \
        || fail "le dépôt contient des modifications locales ; la mise à jour automatique est arrêtée."
    git -C "$LEAF_ROOT" pull --ff-only
    LEAF_VERSION=$(tr -d '[:space:]' < "$LEAF_ROOT/VERSION")
fi

install_apt() {
    (( with_dependencies )) || return 0
    as_root apt-get update
    as_root apt-get install -y --no-install-recommends "$@"
}

version_at_least() {
    [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" == "$2" ]]
}

ensure_rust() {
    install_apt build-essential curl pkg-config openssl ca-certificates
    if command -v cargo >/dev/null; then
        local found
        found=$(cargo --version | awk '{print $2}')
        version_at_least "$found" "1.90.0" && return 0
    fi

    if command -v rustup >/dev/null; then
        rustup toolchain install 1.90.0 --profile minimal
        rustup default 1.90.0
    else
        curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o "$LEAF_TMP/rustup.sh"
        sh "$LEAF_TMP/rustup.sh" -y --profile minimal --default-toolchain 1.90.0
    fi
    # shellcheck disable=SC1091
    source "$HOME/.cargo/env"
}

existing_server_value() {
    local name=$1 value
    as_root test -f /etc/leaf/server.env || return 0
    value=$(as_root sed -n "s/^${name}=//p" /etc/leaf/server.env | tail -n1)
    # Values written by this installer are quoted for systemd. Remove that outer pair
    # before reusing a setting during an update.
    value=${value#\"}
    value=${value%\"}
    printf '%s' "$value"
}

quote_environment_value() {
    local value=$1
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    printf '"%s"' "$value"
}

server_key_from() {
    local keys=$1 first rest
    first=${keys%% *}
    rest=${first#*:}
    [[ "$rest" != "$first" ]] || return 1
    printf '%s' "${rest%%:*}"
}

client_url() {
    local bind=$1 number=$2
    case "$bind" in
        0.0.0.0|::)
            return 1 ;;
        *)
            printf 'http://%s:%s' "$bind" "$number" ;;
    esac
}

install_server() {
    local existing_keys base unit health_host env_file
    existing_keys=$(existing_server_value LEAF_KEYS)

    library=${library:-$(existing_server_value LEAF_LIBRARY)}
    host=${host:-$(existing_server_value LEAF_HOST)}
    port=${port:-$(existing_server_value LEAF_PORT)}
    library=${library:-/srv/leaf/library}
    host=${host:-127.0.0.1}
    port=${port:-8081}
    [[ "$library" != *[[:space:]]* ]] || fail "le chemin de bibliothèque ne doit pas contenir d’espace."

    [[ "$port" =~ ^[0-9]+$ ]] && (( port > 0 && port < 65536 )) \
        || fail "le port doit être compris entre 1 et 65535."
    ensure_rust
    [[ "$library" == /* ]] || fail "--library attend un chemin absolu."
    [[ "$library$host" != *$'\n'* ]] || fail "un chemin ou une adresse contient un retour à la ligne."

    if [[ -n "$existing_keys" ]]; then
        client_key=$(server_key_from "$existing_keys") \
            || fail "LEAF_KEYS existe mais son premier élément n’a pas la forme nom:secret:droits."
    else
        client_key=$(openssl rand -hex 16)
        existing_keys="desktop:$client_key:read,import"
    fi

    cargo build --locked --release --manifest-path "$LEAF_ROOT/server/Cargo.toml"
    as_root install -m 0755 "$LEAF_ROOT/server/target/release/leaf-server" /usr/local/bin/leaf-server
    as_root install -m 0755 "$LEAF_ROOT/install.sh" /usr/local/bin/leaf-install
    as_root install -d -m 0755 /usr/local/share/leaf
    printf '%s\n' "$LEAF_VERSION" > "$LEAF_TMP/server.version"
    as_root install -m 0644 "$LEAF_TMP/server.version" /usr/local/share/leaf/server.version

    base=$(dirname -- "$library")
    as_root install -d -m 0755 -o "$install_user" -g "$install_group" \
        "$library" "$base/inbox" "$base/drop"

    if ! as_root test -f /etc/leaf/server.env; then
    env_file="$LEAF_TMP/server.env"
    {
        printf 'LEAF_LIBRARY=%s\n' "$(quote_environment_value "$library")"
        printf 'LEAF_INBOX=%s\n' "$(quote_environment_value "$base/inbox")"
        printf 'LEAF_DROP=%s\n' "$(quote_environment_value "$base/drop")"
        printf 'LEAF_CACHE=/var/cache/leaf\n'
        printf 'LEAF_DB=/var/lib/leaf/leaf.sqlite\n'
        printf 'LEAF_KEYS=%s\n' "$(quote_environment_value "$existing_keys")"
        printf 'LEAF_HOST=%s\n' "$(quote_environment_value "$host")"
        printf 'LEAF_PORT=%s\n' "$port"
        printf 'LEAF_MAX_UPLOAD_MB=2048\n'
        printf 'LEAF_TRUST_PROXY=\n'
        printf 'LEAF_TLS_CERT=\n'
        printf 'LEAF_TLS_KEY=\n'
        printf 'LEAF_TLS_HOSTS=\n'
    } > "$env_file"
    as_root install -D -m 0600 -o root -g root "$env_file" /etc/leaf/server.env
    fi

    unit="$LEAF_TMP/leaf-server.service"
    {
        printf '%s\n' '[Unit]'
        printf '%s\n' 'Description=Leaf — serveur de bandes dessinées'
        printf 'RequiresMountsFor=%s\n' "$base"
        printf '%s\n' 'After=network-online.target' 'Wants=network-online.target' ''
        printf '%s\n' '[Service]' 'Type=exec'
        printf 'User=%s\nGroup=%s\n' "$install_user" "$install_group"
        printf '%s\n' 'EnvironmentFile=/etc/leaf/server.env'
        printf '%s\n' 'ExecStart=/usr/local/bin/leaf-server serve'
        printf '%s\n' 'Restart=on-failure' 'RestartSec=5s' 'TimeoutStopSec=20s'
        printf '%s\n' 'StateDirectory=leaf' 'CacheDirectory=leaf'
        printf '%s\n' 'NoNewPrivileges=yes' 'ProtectSystem=strict' 'ProtectHome=read-only'
        printf '%s\n' 'PrivateTmp=yes' 'PrivateDevices=yes' 'ProtectKernelTunables=yes'
        printf '%s\n' 'ProtectKernelModules=yes' 'ProtectControlGroups=yes'
        printf '%s\n' 'RestrictSUIDSGID=yes' 'RestrictNamespaces=yes' 'RestrictRealtime=yes'
        printf '%s\n' 'LockPersonality=yes' 'MemoryDenyWriteExecute=yes'
        printf '%s\n' 'RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX'
        printf '%s\n' 'SystemCallFilter=@system-service' 'SystemCallErrorNumber=EPERM'
        printf 'ReadWritePaths=%s\n\n' "$base"
        printf '%s\n' '[Install]' 'WantedBy=multi-user.target'
    } > "$unit"
    as_root install -m 0644 "$unit" /etc/systemd/system/leaf-server.service
    as_root systemctl daemon-reload
    as_root systemctl enable --now leaf-server.service
    as_root systemctl restart leaf-server.service

    health_host=$host
    [[ "$health_host" == "0.0.0.0" ]] && health_host=127.0.0.1
    for _ in $(seq 1 40); do
        curl -fsS "http://$health_host:$port/health" >/dev/null && break
        sleep 0.25
    done
    curl -fsS "http://$health_host:$port/health" >/dev/null \
        || fail "le service est installé mais /health ne répond pas ; consultez journalctl -u leaf-server."

    if [[ -z "$address" ]]; then
        address=$(client_url "$host" "$port" || true)
    fi
}

read_client_configuration() {
    local config="$install_home/.config/Leaf/leaf.conf"
    as_root test -f "$config" || return 0
    [[ -n "$address" ]] || address=$(as_root sed -n 's/^address=//p' "$config" | tail -n1)
    [[ -n "$client_key" ]] || client_key=$(as_root sed -n 's/^key=//p' "$config" | tail -n1)
}

ask_client_configuration() {
    read_client_configuration
    if [[ -n "$key_file" ]]; then
        client_key=$(tr -d '\r\n' < "$key_file")
    fi
    if [[ -z "$address" && -t 0 ]]; then
        read -r -p 'Adresse du serveur Leaf (ex. https://leaf.example.net) : ' address
    fi
    if [[ -z "$client_key" && -t 0 ]]; then
        read -r -s -p 'Clé du client : ' client_key
        printf '\n'
    fi
    [[ -n "$address" && -n "$client_key" ]] \
        || fail "configuration manquante : relancez avec un terminal, --address et --key-file."
    [[ "$address" == http://* || "$address" == https://* ]] \
        || fail "l’adresse doit commencer par http:// ou https://."
}

install_client() {
    install_apt build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev \
        libqt6svg6-dev qml6-module-qtquick qml6-module-qtquick-controls \
        qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtqml \
        qml6-module-qtqml-workerscript qml6-module-qt5compat-graphicaleffects \
        qtkeychain-qt6-dev

    cmake -S "$LEAF_ROOT/desktop" -B "$LEAF_ROOT/desktop/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build "$LEAF_ROOT/desktop/build" --target leaf-desktop
    as_root cmake --install "$LEAF_ROOT/desktop/build"
    as_root install -m 0755 "$LEAF_ROOT/install.sh" /usr/local/bin/leaf-install
    as_root install -d -m 0755 /usr/local/share/leaf
    printf '%s\n' "$LEAF_VERSION" > "$LEAF_TMP/client.version"
    as_root install -m 0644 "$LEAF_TMP/client.version" /usr/local/share/leaf/client.version

    as_root install -d -m 0700 -o "$install_user" -g "$install_group" "$install_home/.config/Leaf"
    {
        printf 'address=%s\n' "$address"
        printf 'key=%s\n' "$client_key"
    } > "$LEAF_TMP/leaf.conf"
    as_root install -m 0600 -o "$install_user" -g "$install_group" \
        "$LEAF_TMP/leaf.conf" "$install_home/.config/Leaf/leaf.conf"

    command -v update-desktop-database >/dev/null && \
        as_root update-desktop-database /usr/local/share/applications >/dev/null || true
    command -v gtk-update-icon-cache >/dev/null && \
        as_root gtk-update-icon-cache -q /usr/local/share/icons/hicolor || true
}

case "$component" in
    server)
        install_server ;;
    client)
        ask_client_configuration
        install_client ;;
    all)
        host=${host:-127.0.0.1}
        install_server
        address=${address:-http://127.0.0.1:$port}
        install_client ;;
esac

printf '\nLeaf %s installé : %s.\n' "$LEAF_VERSION" "$component"
if [[ "$component" == "server" ]]; then
    printf 'Adresse client : %s\n' "${address:-à renseigner selon votre proxy ou adresse réseau}"
    printf 'Clé client     : %s\n' "$client_key"
    printf 'Sur le PC client, lancez leaf-install client et saisissez ces deux valeurs.\n'
elif [[ "$component" == "client" || "$component" == "all" ]]; then
    printf 'Le client est configuré pour %s et apparaît dans le menu des applications.\n' "$address"
fi
