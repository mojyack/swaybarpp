# config

Config is JSON with Comments.

## top-level (window appearance)
All optional.

- `height`(number): bar height in pixels. default `24`
- `position`(string): `"top"` or `"bottom"`. default `"top"`
- `font`(string): Pango font description. List installed families with `fc-list : family`. default `"monospace 11"`
- `background`(string): `#rrggbb` or `#rrggbbaa`. default `#1d1f21`
- `foreground`(string): `#rrggbb` or `#rrggbbaa`. default `#c5c8c6`
- `mods_left`(object): modules packed from the left edge (first entry leftmost)
- `mods_right`(object): modules packed from the right edge (first entry rightmost)

`mods_left`/`mods_right` map a module name to its config object, e.g.
`"cpu_usage": {"prefix": "CPU"}`.

# common parameters
Accepted by every module.

- `on_click`(array of string): argv for an external command run on left-click
  Replaces the module's own click action.

# modules
## padding
Blank spacer that just consumes space.
- `size`(number): width in pixels. default `8`

## workspace
sway workspace switcher (sway IPC). Click a workspace to switch to it. Filters to the bar's own output.
- `socket`(string): path to the sway IPC socket. default `$SWAYSOCK`

## datetime
- `format`(string): strftime format string. default `"%Y-%m-%d %H:%M"`

## cpu_usage
- `prefix`(string): default `"CPU"`

## ram_usage
- `prefix`(string): default `"MEM"`

## loadavg
- `prefix`(string): default `"LA"`

## temp
Reads a hwmon sysfs temperature input.
- `path`(string, required): sysfs file, e.g. `/sys/class/hwmon/hwmon0/temp3_input`
- `prefix`(string): default `""`

## battery
Reads `/sys/class/power_supply/<name>/`.
- `name`(string, required): power-supply name, e.g. `BAT0`
- `charge`(string, required): sysfs metric base, `charge` or `energy` (reads `<charge>_now` / `<charge>_full`)
- `prefix`(string): default `"BAT"`

## network
Active default-route interface
- `prefix`(string): default `""`
- `offline`(string): shown alone when there is no default route. default `"!"`

## pipewire
Native libpipewire. Click to mute, scroll to change volume.
- `prefix`(string): drawn before the volume percentage. default `"VOL"`
- `prefix_muted`(string): shown alone when muted. default `"MUTE"`

## file
Reads a file and draws its contents (newlines stripped).
- `path`(string, required): file to read
- `prefix`(string): drawn before the contents. default `""`
- `suffix`(string): drawn after the contents. default `""`
