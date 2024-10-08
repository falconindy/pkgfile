command_not_found_handler() {
  local pkgs cmd="$1"

  pkgs=(${(f)"$(pkgfile -b -v -- "$cmd" 2>/dev/null)"})
  if [[ -n "$pkgs" ]]; then
    printf '%s may be found in the following packages:\n' "$cmd"
    printf '  %s\n' $pkgs[@]
    if [[ -n $PKGFILE_PROMPT_INSTALL_MISSING ]]; then
      pkg=`echo $pkgs | cut -f1 -d " "`
      echo -n "Install $pkg? [Y/n]"
      read -r response
      [[ -z $response || $response = [Yy] ]] || return 0
      printf '\n'
      sudo pacman -S --noconfirm -- "$pkg"
      return
    fi
  else
    printf 'zsh: command not found: %s\n' "$cmd"
  fi 1>&2

  return 127
}
