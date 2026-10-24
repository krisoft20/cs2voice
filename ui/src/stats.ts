import type { Stats } from './bridge'
import type { Key } from './i18n'
import { REFERENCE } from './reference'

export type Measure = {
  key: Key
  unit: string
  digits: number
  of: (s: Stats) => number | null
}

export const per = (n: number, d: number) => (d > 0 ? n / d : null)
export const pct = (n: number, d: number) => (d > 0 ? (100 * n) / d : null)

export const rating = (s: Stats) => {
  if (s.rounds <= 0) return null
  const kpr = s.kills / s.rounds
  const dpr = s.deaths / s.rounds
  const apr = s.assists / s.rounds
  const kast = (100 * s.kast) / s.rounds
  const adr = s.damage / s.rounds
  const impact = 2.13 * kpr + 0.42 * apr - 0.41
  return 0.0073 * kast + 0.3591 * kpr - 0.5329 * dpr + 0.2372 * impact + 0.0032 * adr + 0.1587
}

export const percentile = (q: number[], x: number) => {
  if (x < q[0]) return 0
  if (x > q[20]) return 100
  let a = 0
  let b = 20
  while (a < 20 && q[a] < x) a++
  while (b > 0 && q[b] > x) b--
  if (a <= b) return (5 * (a + b)) / 2
  return 5 * (b + (x - q[b]) / (q[a] - q[b]))
}

export const utility = (s: Stats) => {
  if (s.rounds <= 0 || s.flashes === undefined) return null
  const parts = [
    percentile(REFERENCE.utility_damage, s.utility_damage / s.rounds),
    percentile(REFERENCE.flash_assists, s.flash_assists / s.rounds),
    100 - percentile(REFERENCE.team_blind, s.team_blind_ms / 1000 / s.rounds),
  ]
  if (s.flashes > 0)
    parts.push(percentile(REFERENCE.flash_blinds, s.enemy_blinds / s.flashes), percentile(REFERENCE.flash_blind, s.enemy_blind_ms / 1000 / s.flashes))
  return parts.reduce((x, y) => x + y, 0) / parts.length
}

export const MEASURES: Measure[] = [
  { key: 'rating', unit: '', digits: 2, of: rating },
  { key: 'kd', unit: '', digits: 2, of: s => per(s.kills, s.deaths) },
  { key: 'adr', unit: '', digits: 1, of: s => per(s.damage, s.rounds) },
  { key: 'kast', unit: '%', digits: 1, of: s => pct(s.kast, s.rounds) },
  { key: 'accuracy', unit: '%', digits: 1, of: s => pct(s.hits, s.shots) },
  { key: 'headAccuracy', unit: '%', digits: 1, of: s => pct(s.head_hits, s.hits) },
  { key: 'wallbangKills', unit: '%', digits: 1, of: s => pct(s.wallbang_kills, s.kills) },
  { key: 'smokeKills', unit: '%', digits: 1, of: s => pct(s.smoke_kills, s.kills) },
  { key: 'utilityRating', unit: '', digits: 0, of: utility },
]

export const median = (v: number[]) => {
  if (!v.length) return null
  const s = [...v].sort((a, b) => a - b)
  const m = s.length >> 1
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2
}

export const bins = (values: number[], count: number) => {
  const lo = Math.min(...values)
  const hi = Math.max(...values)
  const width = hi > lo ? (hi - lo) / count : 1
  const counts = new Array(count).fill(0)
  const at = (v: number) => Math.min(count - 1, Math.max(0, Math.floor((v - lo) / width)))
  for (const v of values) counts[at(v)]++
  return { lo, hi, width, counts, at }
}
