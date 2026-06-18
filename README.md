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
`"cpu_usage": {"format": "CPU {}"}`.

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
- `format`(string): default `"CPU {}%"`

## ram_usage
- `format`(string): default `"MEM {}G"`

## loadavg
- `format`(string): default `"LA {}"`

## temp
Reads a hwmon sysfs temperature input.
- `path`(string, required): sysfs file, e.g. `/sys/class/hwmon/hwmon0/temp3_input`
- `format`(string): default `"{}°C"`

## battery
Reads `/sys/class/power_supply/<name>/`. Click to cycle the display style.
- `name`(string, required): power-supply name, e.g. `BAT0`
- `charge`(string): sysfs metric base, `charge` or `energy` (reads `<charge>_now` / `<charge>_full`)
- `format`(string): wraps the value in `simple` style. default `"BAT {}%"`
- `style`(string): initial display style. default `"simple"`
  - `simple`: text style
  - `graph`: battery icon graphic
- `size`(number): icon height as a fraction of the bar height, `graph` style only. default `0.5`

## network
Active default-route interface. The interface name fills `{}`; a name starting
with `wl` is treated as wireless, otherwise wired.
- `wired`(string): format for a wired interface. default `"{}"`
- `wireless`(string): format for a wireless interface (name starts with `wl`). default `"{}"`
- `offline`(string): shown alone when there is no default route. default `"!"`

## pipewire
Native libpipewire. Click to mute, scroll to change volume.
- `format`(string): wraps the volume percentage. default `"VOL {}%"`
- `format_muted`(string): shown alone when muted. default `"MUTE"`

## file
Reads a file and draws its contents (newlines stripped).
- `path`(string, required): file to read
- `format`(string): wraps the contents. default `"{}"`
