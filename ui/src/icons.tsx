export const SteamIcon = () => (
  <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
    <path fill="currentColor" d="M11.979 0C5.678 0 .511 4.86.022 11.037l6.432 2.658c.545-.371 1.203-.59 1.912-.59.063 0 .125.004.188.006l2.861-4.142V8.91c0-2.495 2.028-4.524 4.524-4.524 2.494 0 4.524 2.031 4.524 4.527s-2.03 4.525-4.524 4.525h-.105l-4.076 2.911c0 .052.004.105.004.159 0 1.875-1.515 3.396-3.39 3.396-1.635 0-3.016-1.173-3.331-2.727L.436 15.27C1.862 20.307 6.486 24 11.979 24c6.627 0 11.999-5.373 11.999-12S18.605 0 11.979 0zM7.54 18.21l-1.473-.61c.262.543.714.999 1.314 1.25 1.297.539 2.793-.076 3.332-1.375.263-.63.264-1.319.005-1.949s-.75-1.121-1.377-1.383c-.624-.26-1.29-.249-1.878-.03l1.523.63c.956.4 1.409 1.5 1.009 2.455-.397.957-1.497 1.41-2.454 1.012H7.54zm11.415-9.303c0-1.662-1.353-3.015-3.015-3.015-1.665 0-3.015 1.353-3.015 3.015 0 1.665 1.35 3.015 3.015 3.015 1.663 0 3.015-1.35 3.015-3.015zm-5.273-.005c0-1.252 1.013-2.266 2.265-2.266 1.249 0 2.266 1.014 2.266 2.266 0 1.251-1.017 2.265-2.266 2.265-1.253 0-2.265-1.014-2.265-2.265z" />
  </svg>
)

const tabler = (paths: string[]) => () => (
  <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {paths.map(d => (
      <path key={d} d={d} />
    ))}
  </svg>
)

export const SettingsIcon = tabler(["M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 -1.065", "M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0"])

export const DemosIcon = tabler(["M13 5h8", "M13 9h5", "M13 15h8", "M13 19h5", "M3 5a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v4a1 1 0 0 1 -1 1h-4a1 1 0 0 1 -1 -1l0 -4", "M3 15a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v4a1 1 0 0 1 -1 1h-4a1 1 0 0 1 -1 -1l0 -4"])

export const FolderIcon = tabler(["M5 4h4l3 3h7a2 2 0 0 1 2 2v8a2 2 0 0 1 -2 2h-14a2 2 0 0 1 -2 -2v-11a2 2 0 0 1 2 -2"])

export const LanguageIcon = tabler(["M9 6.371c0 4.418 -2.239 6.629 -5 6.629", "M4 6.371h7", "M5 9c0 2.144 2.252 3.908 6 4", "M12 20l4 -9l4 9", "M19.1 18h-6.2", "M6.694 3l.793 .582"])

export const PaletteIcon = tabler(["M12 21a9 9 0 0 1 0 -18c4.97 0 9 3.582 9 8c0 1.06 -.474 2.078 -1.318 2.828c-.844 .75 -1.989 1.172 -3.182 1.172h-2.5a2 2 0 0 0 -1 3.75a1.3 1.3 0 0 1 -1 2.25", "M8.5 10.5m-1 0a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", "M12.5 7.5m-1 0a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", "M16.5 10.5m-1 0a1 1 0 1 0 2 0a1 1 0 1 0 -2 0"])

export const DiskIcon = tabler(["M12 6m-8 0a8 3 0 1 0 16 0a8 3 0 1 0 -16 0", "M4 6v6a8 3 0 0 0 16 0v-6", "M4 12v6a8 3 0 0 0 16 0v-6"])

export const InfoIcon = tabler(["M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0", "M12 9h.01", "M11 12h1v4h1"])

export const FolderPlusIcon = tabler(["M12 19h-7a2 2 0 0 1 -2 -2v-11a2 2 0 0 1 2 -2h4l3 3h7a2 2 0 0 1 2 2v3.5", "M16 19h6", "M19 16v6"])

export const RefreshIcon = tabler(["M20 11a8.1 8.1 0 0 0 -15.5 -2m-.5 -4v4h4", "M4 13a8.1 8.1 0 0 0 15.5 2m.5 4v-4h-4"])

export const CloseIcon = tabler(["M18 6l-12 12", "M6 6l12 12"])

export const SearchIcon = tabler(["M10 10m-7 0a7 7 0 1 0 14 0a7 7 0 1 0 -14 0", "M21 21l-6 -6"])

export const ChevronLeftIcon = tabler(["M15 6l-6 6l6 6"])

export const ChevronRightIcon = tabler(["M9 6l6 6l-6 6"])

export const ChevronDownIcon = tabler(["M6 9l6 6l6 -6"])

export const PlusIcon = tabler(["M12 5l0 14", "M5 12l14 0"])

export const MinusIcon = tabler(["M5 12l14 0"])

export const CheckIcon = tabler(["M5 12l5 5l10 -10"])

export const BoltIcon = tabler(["M13 3l0 7l6 0l-8 11l0 -7l-6 0l8 -11"])

export const TradeIcon = tabler(["M7 10h14l-4 -4", "M17 14h-14l4 4"])

export const SunIcon = tabler(["M12 12m-4 0a4 4 0 1 0 8 0a4 4 0 1 0 -8 0", "M3 12h1m8 -9v1m8 8h1m-9 8v1m-6.4 -15.4l.7 .7m12.1 -.7l-.7 .7m0 11.4l.7 .7m-12.1 -.7l-.7 .7"])

export const TargetIcon = tabler(["M12 12m-1 0a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", "M12 12m-5 0a5 5 0 1 0 10 0a5 5 0 1 0 -10 0", "M12 12m-9 0a9 9 0 1 0 18 0a9 9 0 1 0 -18 0"])

export const WallIcon = tabler(["M4 6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2z", "M4 8h16", "M20 12h-16", "M4 16h16", "M9 4v4", "M14 8v4", "M8 12v4", "M16 12v4", "M11 16v4"])

export const CloudIcon = tabler(["M6.657 18c-2.572 0 -4.657 -2.007 -4.657 -4.483c0 -2.475 2.085 -4.482 4.657 -4.482c.393 -1.762 1.794 -3.2 3.675 -3.773c1.88 -.572 3.956 -.193 5.444 1c1.488 1.19 2.162 3.007 1.77 4.769h.99c1.913 0 3.464 1.56 3.464 3.486c0 1.927 -1.551 3.487 -3.465 3.487h-11.878"])

export const CrownIcon = tabler(["M12 6l4 6l5 -4l-2 10h-14l-2 -10l5 4z"])

export const DoorIcon = tabler(["M13 12v.01", "M3 21h18", "M5 21v-16a2 2 0 0 1 2 -2h6m4 10.5v7.5", "M21 7h-7m3 -3l-3 3l3 3"])

export const ShieldIcon = tabler(["M11.46 20.846a12 12 0 0 1 -7.96 -14.846a12 12 0 0 0 8.5 -3a12 12 0 0 0 8.5 3a12 12 0 0 1 -.09 7.06", "M15 19l2 2l4 -4"])

export const FlameIcon = tabler(["M12 10.941c2.333 -3.308 .167 -7.823 -1 -8.941c0 3.395 -2.235 5.299 -3.667 6.706c-1.43 1.408 -2.333 3.621 -2.333 5.588c0 3.704 3.134 6.706 7 6.706s7 -3.002 7 -6.706c0 -1.712 -1.232 -4.403 -2.333 -5.588c-2.084 3.353 -3.257 3.353 -4.667 2.235"])

export const MicIcon = tabler(["M9 5a3 3 0 0 1 6 0v5a3 3 0 0 1 -6 0z", "M5 10a7 7 0 0 0 14 0", "M8 21l8 0", "M12 17l0 4"])

export const MicOffIcon = tabler(["M3 3l18 18", "M9 5a3 3 0 0 1 6 0v5a3 3 0 0 1 -.13 .874m-2 2a3 3 0 0 1 -3.87 -2.872v-1", "M5 10a7 7 0 0 0 10.846 5.85m2 -2a6.967 6.967 0 0 0 1.152 -3.85", "M8 21l8 0", "M12 17l0 4"])

export const LockIcon = tabler(["M5 13a2 2 0 0 1 2 -2h10a2 2 0 0 1 2 2v6a2 2 0 0 1 -2 2h-10a2 2 0 0 1 -2 -2v-6z", "M11 16a1 1 0 1 0 2 0a1 1 0 0 0 -2 0", "M8 11v-4a4 4 0 1 1 8 0v4"])

export const UserMinusIcon = tabler(["M8 7a4 4 0 1 0 8 0a4 4 0 0 0 -8 0", "M6 21v-2a4 4 0 0 1 4 -4h4c.348 0 .686 .045 1.008 .128", "M16 19h6"])

export const BombIcon = tabler(["M15.349 5.349l3.301 3.301a1.2 1.2 0 0 1 0 1.698l-.972 .972a7.5 7.5 0 1 1 -5 -5l.972 -.972a1.2 1.2 0 0 1 1.698 0z", "M17 7l1.5 -1.5", "M19 5l1 -1", "M8 12a3.5 3.5 0 0 1 3.5 -3.5"])

export const UsersIcon = tabler(["M9 7m-4 0a4 4 0 1 0 8 0a4 4 0 1 0 -8 0", "M3 21v-2a4 4 0 0 1 4 -4h4a4 4 0 0 1 4 4v2", "M16 3.13a4 4 0 0 1 0 7.75", "M21 21v-2a4 4 0 0 0 -3 -3.85"])

export const PlayIcon = tabler(["M7 4v16l13 -8z"])

export const CalendarIcon = tabler(["M4 7a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2v-12z", "M16 3v4", "M8 3v4", "M4 11h16"])

export const ClockIcon = tabler(["M3 12a9 9 0 1 0 18 0a9 9 0 0 0 -18 0", "M12 7v5l3 3"])

export const HelpIcon = tabler(["M12 12m-9 0a9 9 0 1 0 18 0a9 9 0 1 0 -18 0", "M12 17v.01", "M12 13.5a1.5 1.5 0 0 1 1 -1.5a2.6 2.6 0 1 0 -3 -4"])

export const LogoMark = () => (
  <svg viewBox="0 0 32 32" width="28" height="28" aria-hidden="true">
    <rect x="1.2" y="9.1" width="4.5" height="13.8" rx="2.25" fill="var(--t)" />
    <rect x="7.6" y="4.9" width="4.5" height="22.2" rx="2.25" fill="var(--t)" />
    <rect x="13.75" y="1" width="4.5" height="30" rx="2.25" fill="var(--signal)" />
    <rect x="19.9" y="4.9" width="4.5" height="22.2" rx="2.25" fill="var(--ct)" />
    <rect x="26.3" y="9.1" width="4.5" height="13.8" rx="2.25" fill="var(--ct)" />
  </svg>
)

export const Speaker = (props: { on: boolean }) => (
  <svg className={`speaker${props.on ? ' on' : ''}`} viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
    <path
      d="M6 15h-2a1 1 0 0 1 -1 -1v-4a1 1 0 0 1 1 -1h2l3.5 -4.5a.8 .8 0 0 1 1.5 .5v14a.8 .8 0 0 1 -1.5 .5l-3.5 -4.5"
      fill={props.on ? 'currentColor' : 'none'}
      stroke="currentColor"
      strokeWidth="1.8"
      strokeLinejoin="round"
    />
    {props.on && <path d="M15 8a5 5 0 0 1 0 8M17.7 5a9 9 0 0 1 0 14" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" />}
  </svg>
)
