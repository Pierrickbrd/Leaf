#!/usr/bin/env bash
set -Eeuo pipefail

# The sources sit beside the script only when the script is a file. Arriving as a string
# (`bash -c "$(curl …)"`) or through a pipe (`bash <(curl …)`), it has no folder of its own,
# and the current directory is not one to trust: this runs as root and builds what it finds.
# LEAF_ROOT stays empty then, and the sources are downloaded.
LEAF_ROOT=""
if [[ -n ${BASH_SOURCE[0]:-} && -f ${BASH_SOURCE[0]} ]]; then
    LEAF_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
fi

is_checkout() {
    local root=$1
    [[ -n "$root" && -f "$root/VERSION" && -d "$root/server" && -d "$root/desktop" ]]
}

bootstrap_from_github() {
    is_checkout "$LEAF_ROOT" && return 0
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

cleanup() {
    local status=$?
    # A device `all` enrolled whose client was never installed: its key was only ever in this
    # process, so the device would stay on the list with nobody holding the key, and the
    # next run would stop on « nom déjà pris ». Found when the client build failed on a
    # missing zlib after the enrolment.
    if (( status != 0 )) && [[ -n $enrolled_name ]]; then
        as_root /usr/local/bin/leaf-server device revoke "$enrolled_name" \
            || printf 'Leaf: l’appareil « %s » reste relié ; retirez-le avec sudo leaf-server device revoke.\n' \
                "$enrolled_name" >&2
    fi
    rm -r -- "$LEAF_TMP"
    if [[ ${LEAF_BOOTSTRAP_DIR:-} == /tmp/leaf-bootstrap.* && -d ${LEAF_BOOTSTRAP_DIR:-} ]]; then
        rm -r -- "$LEAF_BOOTSTRAP_DIR"
    fi
}

# /srv is where the filesystem standard puts the data a machine serves. Not /var/lib/leaf:
# that is the unit's StateDirectory, which systemd takes over on every start, and the inbox
# and drop folders created beside the library would mix with the index.
LEAF_DEFAULT_LIBRARY=/srv/leaf/library

enrolled_name=""
# Set once this run wrote the local client's leaf.conf with a key it enrolled, for the report.
client_written=""
component=""
library=""
host=""
port=""
address=""
public_url=""
client_key=""
key_file=""
# The person installing: the client configuration goes to their home, and `all` shares the
# library with them. Not the account the server runs as, which is service_user.
human_user="${LEAF_INSTALL_USER:-${SUDO_USER:-$(id -un)}}"
service_user=""
share_with=""
binary=""
owned_library_dirs=()
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
           (server and client installed one after the other connect the same way)
  update   download the current release, then reinstall the selected component

Options:
  --library PATH   library directory (asked, default: /srv/leaf/library)
  --host ADDRESS   address the server binds (default: 127.0.0.1)
  --port PORT      server port (default: 8081)
  --address URL    server: its public address, as devices type it (asked otherwise)
                   client: the server to use, with --key-file (both optional)
  --key-file PATH  read the client key from this file
  --user USER      account the server runs as (default: leaf, created if missing)
  --share-with USER  give USER read and write access to the library files
  --binary PATH    install this leaf-server instead of building one here
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

# A function, so that the tests can play the person at the terminal.
at_a_terminal() {
    [[ -t 0 ]]
}

need_value() {
    [[ $# -ge 2 && -n "$2" ]] || fail "$1 attend une valeur."
}

parse_arguments() {
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
                need_value "$@"; service_user=$2; shift 2 ;;
            --share-with)
                need_value "$@"; share_with=$2; shift 2 ;;
            --binary)
                need_value "$@"; binary=$2; shift 2 ;;
            --no-deps)
                with_dependencies=0; shift ;;
            --help|-h)
                usage; exit 0 ;;
            *)
                fail "option inconnue '$1'. Lancez ./install.sh --help." ;;
        esac
    done

    if [[ "$component" == "server" ]]; then
        public_url=$address
        address=""
    fi
    if [[ "$component" == "all" ]]; then
        share_with=${share_with:-$human_user}
        [[ -z "$host" ]] || all_on_the_loopback "$host"
    fi
    # Root's client configuration would land in /root, and sharing a library with root
    # grants nothing root does not already have: this is a sudo run with no SUDO_USER.
    [[ "$share_with" != "root" ]] \
        || fail "partage avec root refusé : lancez avec sudo depuis votre compte, ou fixez LEAF_INSTALL_USER."
}

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

is_loopback() {
    [[ "$1" == 127.0.0.1 || "$1" == localhost || "$1" == ::1 ]]
}

# `all` is a client and its server on one machine, the server on the loopback. Anywhere
# else the server speaks only https, or does not listen on the loopback at all, and the
# local client would get a key for an address that never answers it.
all_on_the_loopback() {
    is_loopback "$1" || fail "install.sh all installe le client et le serveur sur cette machine, le serveur sur 127.0.0.1. Pour un serveur joignable du réseau, lancez install.sh server, puis reliez le client avec le lien qu’il affiche."
}

check_library_path() {
    local path=$1
    [[ "$path" == /* ]] || fail "--library attend un chemin absolu."
    [[ "$path" != *[[:space:]]* ]] || fail "le chemin de bibliothèque ne doit pas contenir d’espace."
    # Resolved first, `..`, `//` and symbolic links included: the refusals below compare
    # text, and /srv/../home is /home.
    path=$(realpath -m -- "$path")
    case "$path" in
        /home|/home/*|/root|/root/*|/run/user|/run/user/*)
            fail "$path est invisible au service (ProtectHome=yes) : choisissez un chemin hors de /home, /root et /run/user." ;;
        /var/lib/leaf|/var/lib/leaf/*|/var/cache/leaf|/var/cache/leaf/*)
            fail "$path est un dossier propre au service, que systemd reprend à chaque démarrage." ;;
    esac
    # The parent becomes ReadWritePaths and receives inbox and drop: at the root, the whole
    # disk would be writable to the server.
    [[ "$(dirname -- "$path")" != / ]] \
        || fail "$path : le dossier parent de la bibliothèque ne peut pas être la racine (exemple : /srv/leaf/library)."
    printf '%s\n' "$path"
}

# The option first, then what an earlier installation recorded, then the person at the
# terminal, then the default — never a question when nobody is there to answer it.
choose_library() {
    local answer
    library=${library:-$(existing_server_value LEAF_LIBRARY)}
    if [[ -z "$library" ]] && at_a_terminal; then
        read -r -p "Emplacement de la bibliothèque [$LEAF_DEFAULT_LIBRARY] : " answer
        library=$answer
    fi
    library=${library:-$LEAF_DEFAULT_LIBRARY}
}

# The address devices type to reach Leaf, through a proxy or a tunnel as much as directly:
# Leaf cannot guess it, and the links of `device add` are made from it. The option first,
# then the person at the terminal; empty is an answer, the line is there to fill later.
choose_public_url() {
    local answer
    if [[ -z "$public_url" ]] && at_a_terminal; then
        read -r -p "Adresse publique de Leaf, telle que les appareils la taperont (ex. https://leaf.example.net ; laisser vide si vous ne savez pas encore) : " answer
        public_url=$answer
    fi
    # Off the loopback the clients speak only https: an http address would make links
    # that no device accepts.
    [[ -z "$public_url" || "$public_url" == https://* ]] \
        || fail "l’adresse publique doit commencer par https://."
}

# An installation that already exists keeps the account it runs as: switching it to `leaf`
# would leave it in front of a library it cannot write to. Only a new one gets `leaf`.
resolve_service_user() {
    local unit=$1 found=""
    if [[ -n "$service_user" ]]; then
        printf '%s\n' "$service_user"
        return
    fi
    [[ -f "$unit" ]] && found=$(sed -n 's/^User=//p' "$unit" | tail -n1)
    printf '%s\n' "${found:-leaf}"
}

ensure_service_account() {
    local name=$1
    getent passwd "$name" >/dev/null && return 0
    [[ "$name" == "leaf" ]] || fail "l’utilisateur '$name' n’existe pas."
    # No password, no shell, no home: an account a flaw in the server could act as, and
    # nothing else.
    as_root useradd --system --user-group --no-create-home --home-dir /nonexistent \
        --shell /usr/sbin/nologin leaf
}

# Creates what is missing and lists in owned_library_dirs every folder the service owns,
# so that sharing reaches one a stopped run created. Never re-owns what exists: a public
# installer handed a path has no business running chown -R on it. For a folder that
# belongs to someone else, it prints the commands instead.
prepare_library_dirs() {
    local user=$1 group=$2 library=$3 base=$4 dir owner
    local -a foreign=()
    owned_library_dirs=()
    for dir in "$library" "$base/inbox" "$base/drop"; do
        # A link the service account made would turn the setfacl run as root on its
        # folders into one on whatever the link points at.
        [[ ! -L "$dir" ]] || fail "$dir est un lien symbolique : remplacez-le par un vrai dossier."
        if [[ ! -e "$dir" ]]; then
            as_root install -d -m 2775 -o "$user" -g "$group" "$dir"
            owned_library_dirs+=("$dir")
        else
            owner=$(stat -c %U -- "$dir")
            if [[ "$owner" == "$user" ]]; then
                owned_library_dirs+=("$dir")
            else
                foreign+=("$dir")
            fi
        fi
    done
    for dir in "${foreign[@]}"; do
        printf 'Leaf: %s existe déjà et appartient à %s ; ses droits ne sont pas modifiés.\n' \
            "$dir" "$(stat -c %U -- "$dir")" >&2
        printf '      Pour le confier au service :\n' >&2
        printf '        sudo chown -R %s:%s %s\n' "$user" "$group" "$dir" >&2
        printf '        sudo setfacl -R -m g:%s:rwX %s\n' "$group" "$dir" >&2
        printf '        sudo find %s -type d -exec setfacl -d -m g:%s:rwX {} +\n' "$dir" "$group" >&2
    done
}

# Root creates the library folders inside the parent and puts ACLs on them. If the service
# account could write in the parent, or in any folder above it, it could rename one away
# and leave a symbolic link in its place, and root would chown or setfacl the target —
# /etc as easily as anything. The kernel is asked as that account, so that its groups and
# any ACL count. A folder anyone can write is refused whether the account exists or not:
# any local account could put the link there.
check_parent_out_of_reach() {
    local user=$1 dir=$2 info owner mode account=0
    if getent passwd "$user" >/dev/null; then
        account=1
        # A runuser that cannot run would read as "cannot write" and pass every folder.
        as_root runuser -u "$user" -- true \
            || fail "runuser ne peut pas agir sous le compte $user : impossible de vérifier les droits du dossier de la bibliothèque."
    fi
    while :; do
        info=$(stat -c '%u %a' -- "$dir" 2>/dev/null || true)
        if [[ -n "$info" ]]; then
            owner=${info%% *}
            mode=${info##* }
            if (( ${mode: -1} & 2 )); then
                fail "$dir est modifiable par tous : un autre compte pourrait y remplacer un dossier par un lien. Choisissez une bibliothèque ailleurs."
            fi
            # Not only the service: any account that can write here could swap a folder for
            # a link while root creates, chowns or ACLs it — the person shared with, or
            # whoever owns a mounted disk.
            if (( owner != 0 )) || (( ${mode: -2:1} & 2 )); then
                fail "$dir n’appartient pas à root seul : un autre compte pourrait y remplacer un dossier par un lien pendant que root le modifie. Rendez-le à root : sudo chown root:root $dir && sudo chmod go-w $dir"
            fi
            if command -v getfacl >/dev/null \
                && getfacl -cp -- "$dir" 2>/dev/null | grep -Eq '^(user|group):[^:]+:.w'; then
                fail "$dir donne par ACL le droit d’écrire à un autre compte : il pourrait y remplacer un dossier par un lien. Retirez-la : sudo setfacl -b $dir"
            fi
        fi
        if (( account )) && as_root runuser -u "$user" -- test -w "$dir"; then
            fail "$dir est modifiable par le compte $user : le service pourrait y remplacer un dossier par un lien. Rendez-le à root : sudo chown root:root $dir && sudo chmod 755 $dir && sudo setfacl -b $dir"
        fi
        [[ "$dir" == / ]] && break
        dir=$(dirname -- "$dir")
    done
}

# The group and an ACL on each folder, the default one included: setgid and the unit's
# UMask cover what the server writes, but a file the person drops in over SSH follows
# their own umask, usually 022, and would stay read-only to the server.
share_library_with() {
    local human=$1 group=$2 dir
    shift 2
    as_root usermod -aG "$group" "$human"
    for dir in "$@"; do
        as_root setfacl -m "g:$group:rwX,u:$human:rwX" "$dir"
        as_root setfacl -d -m "g:$group:rwX,u:$human:rwX" "$dir"
    done
}

# Sharing adds the person to the service account's group. For an installation that runs as
# a person, that group is theirs, and with it their home.
check_shareable_account() {
    local name=$1 uid uid_min
    getent passwd "$name" >/dev/null || return 0
    uid=$(id -u "$name")
    uid_min=$(awk '$1 == "UID_MIN" { print $2 }' /etc/login.defs 2>/dev/null || true)
    (( uid < ${uid_min:-1000} )) \
        || fail "--share-with demande que le serveur tourne sous un compte système, pas sous $name : réinstallez-le avec --user leaf."
}

need_command() {
    local name=$1 package=$2
    command -v "$name" >/dev/null && return 0
    (( with_dependencies )) || fail "$name est nécessaire (paquet $package)."
    install_apt "$package"
}

check_server_binary() {
    local path=$1 name version
    [[ -f "$path" && -x "$path" ]] || fail "$path n’est pas un fichier exécutable."
    read -r name version < <("$path" --version 2>/dev/null || true)
    [[ "$name" == "leaf-server" && -n "$version" ]] || fail "$path n’est pas un binaire leaf-server."
    printf '%s\n' "$version"
}

# Sets server_binary: the one given with --binary, built elsewhere so that this machine
# never receives a toolchain, or else the one built here from the sources. A variable, not
# stdout: inside $(…), bash ignores set -e, and a failed build went on to install whatever
# an earlier one had left in target/.
obtain_server_binary() {
    if [[ -n "$binary" ]]; then
        server_binary=$binary
        return
    fi
    ensure_rust
    cargo build --locked --release --manifest-path "$LEAF_ROOT/server/Cargo.toml"
    server_binary="$LEAF_ROOT/server/target/release/leaf-server"
}

# No key in LEAF_KEYS: each device now gets its own, against a one-time code, and the
# server keeps only its digest. A key made up here would sit in clear in this file.
render_server_env() {
    local library=$1 base=$2 host=$3 port=$4 public_url=$5
    printf 'LEAF_LIBRARY=%s\n' "$(quote_environment_value "$library")"
    printf 'LEAF_INBOX=%s\n' "$(quote_environment_value "$base/inbox")"
    printf 'LEAF_DROP=%s\n' "$(quote_environment_value "$base/drop")"
    printf 'LEAF_CACHE=/var/cache/leaf\n'
    printf 'LEAF_DB=/var/lib/leaf/leaf.sqlite\n'
    printf 'LEAF_KEYS=\n'
    printf 'LEAF_HOST=%s\n' "$(quote_environment_value "$host")"
    printf 'LEAF_PORT=%s\n' "$port"
    printf 'LEAF_PUBLIC_URL=%s\n' "$(quote_environment_value "$public_url")"
    printf 'LEAF_MAX_UPLOAD_MB=2048\n'
    printf 'LEAF_TRUST_PROXY=\n'
    printf 'LEAF_TLS_CERT=\n'
    printf 'LEAF_TLS_KEY=\n'
    printf 'LEAF_TLS_HOSTS=\n'
}

render_server_unit() {
    local user=$1 group=$2 base=$3
    cat <<EOF
[Unit]
Description=Leaf — serveur de bandes dessinées
Documentation=https://github.com/Pierrickbrd/Leaf

# The line not to remove. A mount point that is missing is not unreadable: it is an empty
# folder, perfectly readable. Started with the disk absent, the server would scan an empty
# library, and a scan that finds nothing prunes the index — reading progress included, the
# one thing a new scan cannot rebuild.
RequiresMountsFor=$base

After=network-online.target
Wants=network-online.target

[Service]
Type=exec
User=$user
Group=$group
# What the server writes into the library stays writable by the group it shares the library
# with, as what that group puts there does through the folder's default ACL.
UMask=0002
EnvironmentFile=/etc/leaf/server.env
ExecStart=/usr/local/bin/leaf-server serve
Restart=on-failure
RestartSec=5s
TimeoutStopSec=20s

# The index, the progress and the resized pages live on the system disk.
StateDirectory=leaf
CacheDirectory=leaf
# UMask=0002 is for the library. Here every file would be group-writable, and writing an
# existing file needs no right on its folder: the group shared with a person gets nothing.
StateDirectoryMode=0700
CacheDirectoryMode=0700

NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
RestrictSUIDSGID=yes
RestrictNamespaces=yes
RestrictRealtime=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
SystemCallFilter=@system-service
SystemCallErrorNumber=EPERM
# ProtectSystem=strict makes the rest of the disk read-only. The library, its inbox and its
# drop folder are the only places the server writes outside its own directories.
ReadWritePaths=$base

[Install]
WantedBy=multi-user.target
EOF
}

# Off the loopback the server speaks only https, with a certificate it made itself that
# curl has nothing to check against. --insecure is acceptable only because this asks the
# very machine it runs on; the fingerprint a device must trust travels in its link.
check_health() {
    local bind=$1 number=$2 asked=$1 url
    local -a options=(-fsS)
    case "$asked" in
        0.0.0.0) asked=127.0.0.1 ;;
        ::) asked=::1 ;;
    esac
    [[ "$asked" != *:* ]] || asked="[$asked]"
    if is_loopback "$bind"; then
        url="http://$asked:$number/health"
    else
        options+=(--insecure)
        url="https://$asked:$number/health"
    fi
    for _ in $(seq 1 40); do
        curl "${options[@]}" "$url" >/dev/null && return 0
        sleep 0.25
    done
    curl "${options[@]}" "$url" >/dev/null \
        || fail "le service est installé mais /health ne répond pas ; consultez journalctl -u leaf-server."
}

install_server() {
    local base unit env_file service_group server_binary server_version add_public_url=0
    # Everything is checked before anything is touched: a mistake found after the binary,
    # the account and the folders exist would leave a half-made installation behind.
    server_version=$LEAF_VERSION
    if [[ -n "$binary" ]]; then
        binary=$(realpath -- "$binary")
        server_version=$(check_server_binary "$binary")
    fi

    choose_library
    library=$(check_library_path "$library")
    host=${host:-$(existing_server_value LEAF_HOST)}
    port=${port:-$(existing_server_value LEAF_PORT)}
    host=${host:-127.0.0.1}
    port=${port:-8081}

    [[ "$port" =~ ^[0-9]+$ ]] && (( port > 0 && port < 65536 )) \
        || fail "le port doit être compris entre 1 et 65535."
    # An update never rewrites server.env; it only adds the public address when the line
    # is missing and there is someone to ask, or the option gives it.
    if ! as_root test -f /etc/leaf/server.env; then
        choose_public_url
    elif [[ -n "$public_url" ]] || at_a_terminal; then
        if ! as_root grep -q '^LEAF_PUBLIC_URL=' /etc/leaf/server.env; then
            choose_public_url
            add_public_url=1
        fi
    fi
    [[ "$library$host$public_url" != *$'\n'* ]] || fail "un chemin ou une adresse contient un retour à la ligne."

    base=$(dirname -- "$library")
    service_user=$(resolve_service_user /etc/systemd/system/leaf-server.service)
    if [[ "$service_user" != "leaf" ]]; then
        getent passwd "$service_user" >/dev/null || fail "l’utilisateur '$service_user' n’existe pas."
    fi
    check_parent_out_of_reach "$service_user" "$base"
    if [[ -n "$share_with" ]]; then
        getent passwd "$share_with" >/dev/null || fail "l’utilisateur '$share_with' n’existe pas."
        check_shareable_account "$service_user"
        need_command setfacl acl
    fi

    obtain_server_binary
    as_root install -m 0755 "$server_binary" /usr/local/bin/leaf-server
    as_root install -m 0755 "$LEAF_ROOT/install.sh" /usr/local/bin/leaf-install
    as_root install -d -m 0755 /usr/local/share/leaf
    printf '%s\n' "$server_version" > "$LEAF_TMP/server.version"
    as_root install -m 0644 "$LEAF_TMP/server.version" /usr/local/share/leaf/server.version

    ensure_service_account "$service_user"
    # Again, now that the kernel can be asked as the account: before useradd there was
    # nobody to ask, and a leftover group of that name may hold an ACL on the parent.
    check_parent_out_of_reach "$service_user" "$base"
    service_group=$(id -gn "$service_user")
    prepare_library_dirs "$service_user" "$service_group" "$library" "$base"
    if [[ -n "$share_with" ]]; then
        share_library_with "$share_with" "$service_group" "${owned_library_dirs[@]}"
    fi

    env_file="$LEAF_TMP/server.env"
    if ! as_root test -f /etc/leaf/server.env; then
        render_server_env "$library" "$base" "$host" "$port" "$public_url" > "$env_file"
        as_root install -D -m 0600 -o root -g root "$env_file" /etc/leaf/server.env
    elif (( add_public_url )); then
        as_root cat /etc/leaf/server.env > "$env_file"
        [[ -z $(tail -c1 "$env_file") ]] || printf '\n' >> "$env_file"
        printf 'LEAF_PUBLIC_URL=%s\n' "$(quote_environment_value "$public_url")" >> "$env_file"
        as_root install -m 0600 -o root -g root "$env_file" /etc/leaf/server.env
    fi

    unit="$LEAF_TMP/leaf-server.service"
    render_server_unit "$service_user" "$service_group" "$base" > "$unit"
    as_root install -m 0644 "$unit" /etc/systemd/system/leaf-server.service
    as_root systemctl daemon-reload
    as_root systemctl enable --now leaf-server.service
    as_root systemctl restart leaf-server.service

    check_health "$host" "$port"
}

read_client_configuration() {
    local config="$human_home/.config/Leaf/leaf.conf"
    as_root test -f "$config" || return 0
    [[ -n "$address" ]] || address=$(as_root sed -n 's/^address=//p' "$config" | tail -n1)
    [[ -n "$client_key" ]] || client_key=$(as_root sed -n 's/^key=//p' "$config" | tail -n1)
}

# Nothing to ask: a client with no library opens on the screen that links it, by a
# leaf:// link. --address and --key-file still write leaf.conf, for the scripts
# that pass them; an existing leaf.conf is left as it is.
choose_client_configuration() {
    if [[ -n "$key_file" ]]; then
        client_key=$(tr -d '\r\n' < "$key_file")
    fi
    [[ -n "$address$client_key" ]] || return 0
    read_client_configuration
    [[ -n "$address" && -n "$client_key" ]] \
        || fail "--address et --key-file vont ensemble ; sans eux, le client se relie depuis son propre écran."
    [[ "$address" == http://* || "$address" == https://* ]] \
        || fail "l’adresse doit commencer par http:// ou https://."
}

# The client of this machine is linked by the installer itself, with no code: by `all`, by
# `client` where the server already is, and by `server` where the client already is — in
# whichever order the two arrive. Only `all` used to, and a person who installed one then
# the other on a server bound to the loopback was left on a link screen with no link to
# give it: no other device could reach the library to relay one, and `device add` had no
# address to print. After install_server: before the first start /var/lib/leaf does not
# exist, and device enroll refuses. The key goes from its
# stdout into leaf.conf and nowhere else — never to the screen; and under sudo the
# keyring of the person's session is out of reach. A client that already holds a key keeps
# it: a second device of the same name would be refused.
#
# So does a client whose leaf.conf names an address and no key: it linked itself from its
# own screen — unlinked and linked again, say — and its key is in the keychain, where this
# script cannot see it. Enrolling again would stop on « nom déjà pris » after the server was
# already updated, or make a second device whose key overwrites the file.
link_local_client() {
    local name reach=${host:-127.0.0.1} config="$human_home/.config/Leaf/leaf.conf" linked
    if as_root test -f "$config" \
        && [[ -z $(as_root sed -n 's/^key=//p' "$config") ]]; then
        linked=$(as_root sed -n 's/^address=//p' "$config" | tail -n1)
        if [[ -n "$linked" ]]; then
            address=$linked
            return 0
        fi
    fi
    [[ "$reach" != ::1 ]] || reach="[::1]"
    # The file first: a client that holds a key keeps the address it holds it for. Defaulted
    # first, a remote library's key went into leaf.conf beside http://127.0.0.1, which the
    # local server refused, and the client forgot everything.
    read_client_configuration
    [[ -z "$client_key" ]] || return 0
    address=${address:-http://$reach:$port}
    name=$(uname -n)
    name=${name:0:40}
    # Returned, not failed: whether a refusal stops the run is the caller's call.
    client_key=$(as_root /usr/local/bin/leaf-server device enroll "$name") || client_key=""
    [[ -n "$client_key" ]] || return 1
    enrolled_name=$name
}

# What `all` and `client` say when the client of this machine could not be enrolled: there,
# linking it is what was asked.
ENROLL_REFUSED="leaf-server device enroll n’a pas relié ce client ; voyez sudo leaf-server device list."

# What this machine already holds, read from what this installer itself writes: the
# server's binary and its unit, the client's version file. Nothing is asked of a running
# process or of the package manager — a source build registers with neither.
server_installed_here() {
    as_root test -x /usr/local/bin/leaf-server \
        && as_root test -f /etc/systemd/system/leaf-server.service
}

client_installed_here() {
    as_root test -f /usr/local/share/leaf/client.version
}

# The client of this machine, linked to the server of this machine when that server is on
# the loopback — the one place the client reaches it at http://127.0.0.1 with no certificate
# to pin. Off it the server speaks only https, and the client links from the link `device
# add` prints, which works there. Root has no session of its own to link: under sudo with no
# SUDO_USER, the leaf.conf would land in /root for nobody.
link_client_here() {
    [[ "$human_user" != root ]] || return 0
    is_loopback "${host:-127.0.0.1}" || return 0
    link_local_client
}

# leaf.conf, 0600 and the person's, when this run holds an address and a key for it.
write_client_configuration() {
    [[ -n "$address" && -n "$client_key" ]] || return 0
    as_root install -d -m 0700 -o "$human_user" -g "$human_group" "$human_home/.config/Leaf"
    {
        printf 'address=%s\n' "$address"
        printf 'key=%s\n' "$client_key"
    } > "$LEAF_TMP/leaf.conf"
    as_root install -m 0600 -o "$human_user" -g "$human_group" \
        "$LEAF_TMP/leaf.conf" "$human_home/.config/Leaf/leaf.conf"
    # The key is held by the client from here: a failure after this point must not
    # revoke the device (`cleanup`).
    [[ -z $enrolled_name ]] || client_written=1
    enrolled_name=""
}

# leaf.conf, only for a key this run brought — enrolled now, or given with --key-file. A
# leaf.conf that already held one is left exactly as it was: rewriting it dropped the name,
# the fingerprint and the date the client keeps beside the key, and beside a loopback server
# it swapped a remote library's address for http://127.0.0.1.
save_client_key() {
    if [[ -n $enrolled_name || -n $key_file ]]; then
        write_client_configuration
    fi
}

install_client() {
    install_apt build-essential cmake ninja-build zlib1g-dev qt6-base-dev qt6-declarative-dev \
        libqt6svg6-dev qml6-module-qtquick qml6-module-qtquick-controls \
        qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtqml \
        qml6-module-qtqml-workerscript qml6-module-qt5compat-graphicaleffects \
        qt6-image-formats-plugins qtkeychain-qt6-dev

    cmake -S "$LEAF_ROOT/desktop" -B "$LEAF_ROOT/desktop/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build "$LEAF_ROOT/desktop/build" --target leaf-desktop
    as_root cmake --install "$LEAF_ROOT/desktop/build"
    as_root install -m 0755 "$LEAF_ROOT/install.sh" /usr/local/bin/leaf-install
    as_root install -d -m 0755 /usr/local/share/leaf
    printf '%s\n' "$LEAF_VERSION" > "$LEAF_TMP/client.version"
    as_root install -m 0644 "$LEAF_TMP/client.version" /usr/local/share/leaf/client.version

    save_client_key

    command -v update-desktop-database >/dev/null && \
        as_root update-desktop-database /usr/local/share/applications >/dev/null || true
    # The handler of leaf:// links is the person's choice, in their ~/.config/mimeapps.list:
    # run as root, xdg-mime would set root's. HOME too, since sudo hands on root's.
    command -v xdg-mime >/dev/null && \
        as_root runuser -u "$human_user" -- env HOME="$human_home" \
            xdg-mime default leaf.desktop x-scheme-handler/leaf || true
    command -v gtk-update-icon-cache >/dev/null && \
        as_root gtk-update-icon-cache -q /usr/local/share/icons/hicolor || true
}

main() {
    bootstrap_from_github "$@"
    LEAF_VERSION=$(tr -d '[:space:]' < "$LEAF_ROOT/VERSION")
    LEAF_TMP=$(mktemp -d /tmp/leaf-install.XXXXXX)
    trap cleanup EXIT

    parse_arguments "$@"

    getent passwd "$human_user" >/dev/null || fail "l’utilisateur '$human_user' n’existe pas."
    human_group=$(id -gn "$human_user")
    human_home=$(getent passwd "$human_user" | cut -d: -f6)

    if (( updating )); then
        command -v git >/dev/null || fail "git est nécessaire pour mettre à jour cette installation source."
        [[ -z $(git -C "$LEAF_ROOT" status --porcelain) ]] \
            || fail "le dépôt contient des modifications locales ; la mise à jour automatique est arrêtée."
        git -C "$LEAF_ROOT" pull --ff-only
        LEAF_VERSION=$(tr -d '[:space:]' < "$LEAF_ROOT/VERSION")
    fi

    case "$component" in
        server)
            install_server
            # Optional here: the server is installed whatever happens to its neighbour, and a
            # refusal — another account enrolled this machine's name, say — is said, not
            # turned into a failed installation.
            if client_installed_here; then
                if link_client_here; then
                    save_client_key
                else
                    printf 'Leaf : le client de cette machine n’a pas pu être relié ; ouvrez-y le lien que donne sudo leaf-server device add.\n' >&2
                fi
            fi ;;
        client)
            choose_client_configuration
            # --address and --key-file say where this client goes; without them, a server
            # installed here is the library it belongs to. The key is written by
            # install_client once the client is there, as for `all`: a build that fails
            # first takes its device back (`cleanup`).
            if [[ -z "$address$client_key" ]] && server_installed_here; then
                host=$(existing_server_value LEAF_HOST)
                port=$(existing_server_value LEAF_PORT)
                port=${port:-8081}
                link_client_here || fail "$ENROLL_REFUSED"
            fi
            install_client ;;
        all)
            # An update keeps the host server.env names, and it must be the loopback too.
            host=${host:-$(existing_server_value LEAF_HOST)}
            host=${host:-127.0.0.1}
            all_on_the_loopback "$host"
            install_server
            link_local_client || fail "$ENROLL_REFUSED"
            install_client ;;
    esac

    report_installation
}

# A first link to open on a device, or its QR code to scan, made by device add after the
# server answered /health. Never a key: a key on the screen is a key in a scrollback.
report_server_link() {
    local status=0
    printf '\n'
    as_root /usr/local/bin/leaf-server device add || status=$?
    # 2 is device add's own answer on the loopback without LEAF_PUBLIC_URL: no code made,
    # and the explanation already printed. The installation itself succeeded.
    if (( status != 0 && status != 2 )); then
        printf 'Leaf: aucun lien n’a pu être créé ; relancez sudo leaf-server device add.\n' >&2
    fi
}

report_installation() {
    printf '\nLeaf %s installé : %s.\n' "$LEAF_VERSION" "$component"
    case "$component" in
        server)
            if [[ -n $client_written ]]; then
                printf 'Le client de cette machine est relié au serveur (%s).\n' "$address"
            fi
            report_server_link ;;
        all)
            printf 'Le client de cette machine est relié au serveur (%s) et apparaît dans le menu des applications.\n' "$address"
            printf 'Pour relier un autre appareil : sudo leaf-server device add\n' ;;
        client)
            if [[ -n "$address" ]]; then
                printf 'Le client est configuré pour %s et apparaît dans le menu des applications.\n' "$address"
            else
                printf 'Le client apparaît dans le menu des applications. Pour le relier, ouvrez sur ce PC\n'
                printf 'le lien leaf:// que donne sudo leaf-server device add sur le serveur : cliquez-le, ou collez-le\n'
                printf 'dans l’écran de liaison du client.\n'
            fi ;;
    esac
}

# Sourced by tools/test_install.py with LEAF_INSTALL_SOURCE_ONLY=1, so that each decision
# can be called on its own and nothing is installed.
[[ ${LEAF_INSTALL_SOURCE_ONLY:-} == 1 ]] || main "$@"
