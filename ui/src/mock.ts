import type { Demo, Incoming, Roster, Stats } from './bridge'

const now = Math.floor(Date.now() / 1000)

const faceit: Roster = {
  type: 'roster',
  path: 'C:\\Users\\you\\Downloads\\1-7f3c-1-1.dem.zst',
  ok: true,
  partial: false,
  err: '',
  map: 'de_inferno',
  server: 'FACEIT.com register to play here',
  voice: 39832,
  duration: 1698,
  rounds: 18,
  teams: [
    { name: 'team_banana', score: 5, side: 2 },
    { name: 'team_apartments', score: 13, side: 3 },
  ],
  winners: [2, 3, 3, 3, 2, 3, 3, 2, 3, 3, 3, 2, 3, 2, 2, 3, 2, 2],
  players: [
    { slot: 0, name: 'molotov_mike', steamid: '1', team: 0, voice: 170 },
    { slot: 1, name: 'flashbang', steamid: '2', team: 1, voice: 8801 },
    { slot: 2, name: 'Seddo', steamid: '3', team: 1, voice: 9076 },
    { slot: 3, name: 'ecoround', steamid: '4', team: 1, voice: 2753 },
    { slot: 4, name: 'vaho', steamid: '5', team: 1, voice: 2624 },
    { slot: 5, name: 'you', steamid: '6', team: 0, voice: 4443 },
    { slot: 6, name: 'pyro', steamid: '7', team: 1, voice: 4256 },
    { slot: 7, name: 'quietguy', steamid: '8', team: 0, voice: 0 },
    { slot: 8, name: 'IE3', steamid: '9', team: 0, voice: 6764 },
    { slot: 9, name: 'whatspoint', steamid: '10', team: 0, voice: 945 },
  ],
}

const statsOf = (seed: number): Stats => {
  const r = (n: number) => Math.floor(((Math.sin(seed * 97.3 + n * 13.1) + 1) / 2) * 1000) / 1000
  const kills = 6 + Math.round(r(1) * 20)
  const shots = 120 + Math.round(r(2) * 250)
  const hits = Math.round(shots * (0.1 + r(3) * 0.2))
  return {
    kills,
    deaths: 8 + Math.round(r(4) * 10),
    assists: Math.round(r(5) * 8),
    damage: 900 + Math.round(r(6) * 1200),
    hs_kills: Math.round(kills * r(7) * 0.6),
    wallbang_kills: Math.round(r(8) * 2),
    smoke_kills: Math.round(r(9) * 2),
    rounds: 18,
    kast: 8 + Math.round(r(10) * 9),
    shots,
    hits,
    head_hits: Math.round(hits * (0.1 + r(11) * 0.25)),
    entry_kills: Math.round(r(12) * 5),
    entry_deaths: Math.round(r(13) * 5),
    trade_kills: Math.round(r(14) * 4),
    clutch_rounds: Math.round(r(15) * 4),
    clutches: Math.round(r(16) * 2),
    k2: Math.round(r(17) * 5),
    k3: Math.round(r(18) * 2),
    k4: Math.round(r(19)),
    k5: 0,
    utility_damage: Math.round(r(20) * 200),
    enemies_flashed: Math.round(r(21) * 12),
    flash_assists: Math.round(r(22) * 2),
    flashes: 4 + Math.round(r(23) * 12),
    enemy_blinds: Math.round(r(24) * 14),
    enemy_blind_ms: Math.round(r(25) * 30000),
    team_blind_ms: Math.round(r(26) * 6000),
    lines: Array.from({ length: 18 }, (_, i) => [1, Math.round(r(30 + i) * 2.4), r(60 + i) > 0.45 ? 1 : 0, Math.round(r(90 + i) * 160), i < 12 ? 2 : 3]),
  }
}

for (const p of faceit.players) p.stats = statsOf(Number(p.steamid))

const mm: Roster = {
  ...faceit,
  path: 'D:\\Steam\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo\\replays\\match730_0038_187.dem',
  map: 'de_cache',
  server: 'Valve Counter-Strike 2 eu_north Server',
  voice: 0,
  teams: [
    { name: '', score: 5, side: 2 },
    { name: '', score: 13, side: 3 },
  ],
  players: faceit.players.map(p => ({ ...p, voice: 0 })),
}

const demos: Demo[] = [
  { path: mm.path, name: 'match730_0038_187.dem', folder: 'replays', size: 185645808, mtime: now - 3600, info: mm },
  { path: faceit.path, name: '1-7f3c-1-1.dem.zst', folder: 'Downloads', size: 145778416, mtime: now - 86400 * 2 },
]

export const handle = (m: { type: string; path?: string; text?: string }, deliver: (m: Incoming) => void) => {
  if (m.type === 'ready') {
    deliver({ type: 'folders', items: [{ path: 'D:\\Steam\\...\\replays', custom: false }, { path: 'C:\\Users\\you\\Downloads', custom: false }] })
    deliver({ type: 'demos', items: demos })
    setTimeout(() => deliver(faceit), 1200)
  }
  if (m.type === 'voice' && m.path)
    void fetch('/dev-voice.bin')
      .then(r => (r.ok ? r.arrayBuffer() : Promise.reject(new Error('no dev-voice.bin'))))
      .then(data => deliver({ type: 'voice', path: m.path!, err: '', data }))
      .catch(e => deliver({ type: 'voice', path: m.path!, err: String(e) }))
  if (m.type === 'rescan') setTimeout(() => deliver({ type: 'demos', items: demos }), 300)
  if (m.type === 'open' && m.path === faceit.path) deliver({ type: 'reading', path: faceit.path })
  if (m.type === 'open' && m.path === mm.path) deliver(mm)
  if (m.type === 'copy' && m.text) {
    void navigator.clipboard?.writeText(m.text)
    deliver({ type: 'copied', text: m.text })
  }
}
