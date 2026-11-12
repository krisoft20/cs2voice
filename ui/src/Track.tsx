import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import { send, type EloHistory, type FaceitPlayer, type Stats, type TrackMatch, type TrackStats } from './bridge'
import { Avatar, RatingChip, useUi } from './common'
import { Icon } from './Details'
import { weaponName } from './items'
import type { Key } from './i18n'
import { blend, pct, per, rating2, ratingTier, swingTier } from './stats'
import { mapName, when } from './voice'

type Tab = 'overview' | 'maps' | 'details' | 'matches'
type Source = 'all' | 'faceit' | 'valve' | 'other'
type Range = 'all' | 'last20' | 'last50' | 'days30'
type Row = { m: TrackMatch; s: TrackStats; mine: number; theirs: number; source: Source; cl: [number, number, number, number][]; elo: number | null; wp: Record<string, number> }
type Sum = { matches: number; wins: number; losses: number; elo: number | null; s: Record<string, number> }
type Metric = { key: Key; digits: number; unit: string; of: (a: Sum) => number | null; low?: boolean }

const ROLL = 10
const RECENT = 200
const FORM = 10
const WEEKS = 26

const sourceOf = (server: string): Source => {
  const s = server.toLowerCase()
  return s.includes('faceit') ? 'faceit' : s.includes('valve') ? 'valve' : 'other'
}

const total = (rows: Row[]): Sum => {
  const a: Sum = { matches: rows.length, wins: 0, losses: 0, elo: null, s: {} }
  for (const r of rows) {
    if (r.elo !== null) a.elo = r.elo
    a.wins += r.mine > r.theirs ? 1 : 0
    a.losses += r.mine < r.theirs ? 1 : 0
    for (const [k, v] of Object.entries(r.s)) if (typeof v === 'number') a.s[k] = (a.s[k] ?? 0) + v
  }
  return a
}

const n = (a: Sum, k: string) => a.s[k] ?? 0
const swingOf = (a: Sum) => (a.s.swing === undefined ? null : per(n(a, 'swing'), n(a, 'rounds') * 100))

const METRICS: Metric[] = [
  { key: 'winRate', digits: 0, unit: '%', of: a => pct(a.wins, a.matches) },
  { key: 'rating', digits: 2, unit: '', of: a => blend(a.s as unknown as Stats, swingOf(a)) ?? rating2(a.s as unknown as Stats) },
  { key: 'impact', digits: 2, unit: '', of: a => { const v = swingOf(a); return v === null ? null : 1 + 0.06 * v } },
  { key: 'roundSwing', digits: 2, unit: '%', of: swingOf },
  { key: 'adr', digits: 1, unit: '', of: a => per(n(a, 'damage'), n(a, 'rounds')) },
  { key: 'kd', digits: 2, unit: '', of: a => per(n(a, 'kills'), n(a, 'deaths')) },
  { key: 'kr', digits: 2, unit: '', of: a => per(n(a, 'kills'), n(a, 'rounds')) },
  { key: 'kast', digits: 0, unit: '%', of: a => pct(n(a, 'kast'), n(a, 'rounds')) },
  { key: 'hsPct', digits: 0, unit: '%', of: a => pct(n(a, 'hs_kills'), n(a, 'kills')) },
  { key: 'entrySuccess', digits: 0, unit: '%', of: a => pct(n(a, 'entry_kills'), n(a, 'entry_kills') + n(a, 'entry_deaths')) },
  { key: 'clutchSuccess', digits: 0, unit: '%', of: a => pct(n(a, 'clutches'), n(a, 'clutch_rounds')) },
  { key: 'flashSuccess', digits: 0, unit: '%', of: a => pct(n(a, 'enemy_blinds'), n(a, 'flashes')) },
  { key: 'utilRound', digits: 1, unit: '', of: a => per(n(a, 'utility_damage'), n(a, 'rounds')) },
  { key: 'faceitElo', digits: 0, unit: '', of: a => a.elo },
]

const TOTALS: [Key, (a: Sum) => number][] = [
  ['totalMatches', a => a.matches],
  ['totalWins', a => a.wins],
  ['totalLosses', a => a.losses],
  ['totalRounds', a => n(a, 'rounds')],
  ['totalKills', a => n(a, 'kills')],
  ['totalDeaths', a => n(a, 'deaths')],
  ['totalAssists', a => n(a, 'assists')],
  ['totalDamage', a => n(a, 'damage')],
  ['colEntryKills', a => n(a, 'entry_kills')],
  ['colEntryDeaths', a => n(a, 'entry_deaths')],
  ['colTrades', a => n(a, 'trade_kills')],
  ['colClutch', a => n(a, 'clutches')],
  ['col2k', a => n(a, 'k2')],
  ['col3k', a => n(a, 'k3')],
  ['col4k', a => n(a, 'k4')],
  ['col5k', a => n(a, 'k5')],
  ['colUtil', a => n(a, 'utility_damage')],
  ['colFlashed', a => n(a, 'enemies_flashed')],
  ['colFlashAssists', a => n(a, 'flash_assists')],
  ['colWallbang', a => n(a, 'wallbang_kills')],
]

const metric = (key: Key) => METRICS.find(m => m.key === key)!
const MAIN: Key[] = ['rating', 'winRate', 'kd', 'adr', 'kast', 'roundSwing', 'faceitElo']
const SIDES: [string, Key, string][] = [
  ['t', 'skinsSideT', 'tk-t'],
  ['ct', 'skinsSideCt', 'tk-ct'],
]
const sideWon = (a: Sum, side: string) => pct(n(a, `${side}_wins`), n(a, `${side}_rounds`))
const whole = (v: number | null) => (v === null ? '–' : `${Math.round(v)}%`)
const WEAPONS: [Key, string, string][] = [
  ['wRifle', 'wk_rifle', 'ak47'],
  ['wSniper', 'wk_sniper', 'awp'],
  ['wSmg', 'wk_smg', 'mp9'],
  ['wPistol', 'wk_pistol', 'glock'],
  ['wHeavy', 'wk_heavy', 'nova'],
  ['wKnife', 'wk_knife', 'knife'],
  ['wZeus', 'wk_zeus', 'taser'],
  ['wOther', 'wk_other', 'hegrenade'],
]
const GROUPS: [Key, number[]][] = [
  ['groupFrags', [4, 5, 6, 7, 12, 13, 14, 15]],
  ['groupOpening', [8, 9, 10, 11]],
  ['groupUtility', [16, 17, 18, 19]],
]

const tierOf = (m: Metric, v: number | null) => (m.key === 'rating' || m.key === 'impact' ? ratingTier(v) : m.key === 'roundSwing' ? swingTier(v) : '')
const won = (r: Row) => r.mine > r.theirs
const lost = (r: Row) => r.mine < r.theirs

const show = (v: number | null, m: Metric, lang: string) =>
  v === null ? '–' : `${new Intl.NumberFormat(lang, { minimumFractionDigits: m.digits, maximumFractionDigits: m.digits }).format(v)}${m.unit}`

const rolling = (rows: Row[], m: Metric, size: number) => rows.map((_, i) => m.of(total(rows.slice(Math.max(0, i - size + 1), i + 1))))

function Spark(props: { values: (number | null)[] }) {
  const v = props.values.filter((x): x is number => x !== null)
  if (v.length < 3) return <svg className="tk-spark" aria-hidden="true" />
  const lo = Math.min(...v)
  const hi = Math.max(...v)
  const pts = v.map((x, i) => `${((i / (v.length - 1)) * 100).toFixed(1)},${(28 - ((x - lo) / (hi - lo || 1)) * 26).toFixed(1)}`)
  return (
    <svg className="tk-spark" viewBox="0 0 100 30" preserveAspectRatio="none" aria-hidden="true">
      <polygon points={`0,30 ${pts.join(' ')} 100,30`} />
      <polyline points={pts.join(' ')} />
    </svg>
  )
}

const step = (span: number) => {
  const raw = span / 4
  const pow = 10 ** Math.floor(Math.log10(raw || 1))
  const k = raw / pow
  return (k <= 1 ? 1 : k <= 2 ? 2 : k <= 2.5 ? 2.5 : k <= 5 ? 5 : 10) * pow
}

const smooth = (pts: [number, number][]) => {
  let d = `M${pts[0][0].toFixed(1)} ${pts[0][1].toFixed(1)}`
  for (let i = 0; i < pts.length - 1; i++) {
    const p0 = pts[Math.max(0, i - 1)]
    const p1 = pts[i]
    const p2 = pts[i + 1]
    const p3 = pts[Math.min(pts.length - 1, i + 2)]
    d += `C${(p1[0] + (p2[0] - p0[0]) / 6).toFixed(1)} ${(p1[1] + (p2[1] - p0[1]) / 6).toFixed(1)} ${(p2[0] - (p3[0] - p1[0]) / 6).toFixed(1)} ${(p2[1] - (p3[1] - p1[1]) / 6).toFixed(1)} ${p2[0].toFixed(1)} ${p2[1].toFixed(1)}`
  }
  return d
}

function Chart(props: { rows: Row[]; metric: Metric; label: string; mean: number | null; known: Set<string>; onOpen: (path: string) => void }) {
  const { lang, t } = useUi()
  const { rows, metric, label, mean, known, onOpen } = props
  const box = useRef<HTMLDivElement>(null)
  const [W, setW] = useState(900)
  const [at, setAt] = useState<number | null>(null)
  useEffect(() => {
    const el = box.current
    if (!el) return
    const ro = new ResizeObserver(() => setW(Math.max(320, el.clientWidth)))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])
  const each = useMemo(() => rows.map(r => metric.of(total([r]))), [rows, metric])
  const avg = useMemo(() => rolling(rows, metric, ROLL), [rows, metric])
  const plain = metric.key === 'faceitElo'
  const dots = metric.key !== 'winRate' && !plain
  const all = [...(dots ? each : []), ...avg, mean].filter((x): x is number => x !== null)
  if (all.length < 2 || rows.length < 2) return <div ref={box} className="tk-plot-box" />
  const H = 320
  const L = 46
  const R = 14
  const T = 12
  const B = 30
  const by = step(Math.max(...all) - Math.min(...all))
  const lo = Math.floor(Math.min(...all) / by) * by
  const hi = Math.max(lo + by, Math.ceil(Math.max(...all) / by) * by)
  const ticks: number[] = []
  for (let v = lo; v <= hi + by / 2; v += by) ticks.push(v)
  const x = (i: number) => L + (i / (rows.length - 1)) * (W - L - R)
  const y = (v: number) => H - B - ((v - lo) / (hi - lo)) * (H - T - B)
  const line = avg.map((v, i) => (v === null ? null : ([x(i), y(v)] as [number, number]))).filter((q): q is [number, number] => q !== null)
  const path = line.length < 2 ? '' : plain ? `M${line.map(q => `${q[0].toFixed(1)} ${q[1].toFixed(1)}`).join('L')}` : smooth(line)
  const base = mean === null ? H - B : y(mean)
  const day = new Intl.DateTimeFormat(lang, { day: 'numeric', month: 'short' })
  const marks = Math.min(rows.length, Math.max(2, Math.floor((W - L - R) / 130)))
  const dates = Array.from({ length: marks }, (_, k) => Math.round((k / (marks - 1)) * (rows.length - 1)))
  const axis = { ...metric, unit: '' }
  const hit = (e: React.MouseEvent) => {
    const r = e.currentTarget.getBoundingClientRect()
    const i = Math.round(((e.clientX - r.left - L) / (W - L - R)) * (rows.length - 1))
    setAt(Math.max(0, Math.min(rows.length - 1, i)))
  }
  const cur = at !== null && at < rows.length ? rows[at] : null
  const open = cur ? known.has(cur.m.path) : false
  return (
    <div ref={box} className={`tk-plot-box${open ? ' can' : ''}`} onMouseMove={hit} onMouseLeave={() => setAt(null)} onClick={() => cur && open && onOpen(cur.m.path)}>
      <svg className="tk-chart" width={W} height={H} viewBox={`0 0 ${W} ${H}`} role="img" aria-label={label}>
        {ticks.map(v => (
          <g key={v}>
            <line className="grid" x1={L} x2={W - R} y1={y(v)} y2={y(v)} />
            <text x={L - 10} y={y(v) + 4} textAnchor="end">
              {show(v, axis, lang)}
            </text>
          </g>
        ))}
        {dates.map((i, k) => (
          <text key={i} x={x(i)} y={H - 8} textAnchor={k === 0 ? 'start' : k === dates.length - 1 ? 'end' : 'middle'}>
            {day.format(new Date(rows[i].m.date * 1000))}
          </text>
        ))}
        {path && <path className="area" d={`${path}L${line[line.length - 1][0].toFixed(1)} ${H - B}L${line[0][0].toFixed(1)} ${H - B}Z`} />}
        {mean !== null && !plain && <line className="mean" x1={L} x2={W - R} y1={base} y2={base} />}
        {dots &&
          each.map((v, i) =>
            v === null ? null : (
              <g key={i} className={`${won(rows[i]) ? 'won' : lost(rows[i]) ? 'lost' : ''}${at === i ? ' at' : ''}`}>
                <line className="stem" x1={x(i)} x2={x(i)} y1={base} y2={y(v)} />
                <circle cx={x(i)} cy={y(v)} r={at === i ? 6 : 4} />
              </g>
            ),
          )}
        {path && <path className="avg" d={path} />}
        {at !== null && <line className="guide" x1={x(at)} x2={x(at)} y1={T} y2={H - B} />}
        {at !== null && avg[at] !== null && <circle className="knot" cx={x(at)} cy={y(avg[at]!)} r={4} />}
      </svg>
      {cur && at !== null && (
        <div className={`tk-tip${x(at) > W - 210 ? ' left' : ''}`} style={{ left: x(at), top: T }}>
          <small>{when(cur.m.date, t, lang)}</small>
          <b>
            {mapName(cur.m.map, t)}
            <span className={won(cur) ? 'tk-won' : lost(cur) ? 'tk-lost' : ''}>
              {cur.mine}:{cur.theirs}
            </span>
          </b>
          {dots && (
            <span>
              {label}
              <i className={`tk-${tierOf(metric, each[at])}`}>{show(each[at], metric, lang)}</i>
            </span>
          )}
          <span>
            {plain ? label : t('trackAverage', { n: ROLL })}
            <i>{show(avg[at], metric, lang)}</i>
          </span>
        </div>
      )}
    </div>
  )
}

function Activity(props: { rows: Row[] }) {
  const { lang, t } = useUi()
  const { rows } = props
  const day = (d: Date) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
  const today = new Date()
  const monday = day(today) - ((today.getDay() + 6) % 7) * 86400000
  const first = monday - (WEEKS - 1) * 7 * 86400000
  const byDay = new Map<number, number>()
  const hours = Array.from({ length: 24 }, () => [0, 0])
  const weekdays = Array.from({ length: 7 }, () => [0, 0])
  for (const r of rows) {
    const d = new Date(r.m.date * 1000)
    byDay.set(day(d), (byDay.get(day(d)) ?? 0) + 1)
    const won = r.mine > r.theirs ? 1 : 0
    hours[d.getHours()][0]++
    hours[d.getHours()][1] += won
    weekdays[(d.getDay() + 6) % 7][0]++
    weekdays[(d.getDay() + 6) % 7][1] += won
  }
  const peak = Math.max(1, ...byDay.values())
  const month = new Intl.DateTimeFormat(lang, { month: 'short' })
  const weekday = new Intl.DateTimeFormat(lang, { weekday: 'short' })
  const full = new Intl.DateTimeFormat(lang, { day: 'numeric', month: 'short', year: 'numeric' })
  const bars = (data: number[][], names: string[]) => {
    const top = Math.max(1, ...data.map(d => d[0]))
    return (
      <div className="tk-bars" style={{ gridTemplateColumns: `repeat(${data.length}, 1fr)` }}>
        {data.map((d, i) => (
          <div key={i} className="tk-bar" title={`${names[i]}: ${d[1]} / ${d[0]}`}>
            <i style={{ height: `${(d[0] / top) * 100}%` }}>
              <b style={{ height: d[0] ? `${(d[1] / d[0]) * 100}%` : 0 }} />
            </i>
            <small>{names[i]}</small>
          </div>
        ))}
      </div>
    )
  }
  return (
    <div className="tk-activity">
      <div className="tk-card tk-heat">
        {Array.from({ length: WEEKS }, (_, w) => {
          const start = new Date(first + w * 7 * 86400000)
          const label = start.getDate() <= 7 ? month.format(start) : ''
          return (
            <div key={w} className="tk-week">
              <small>{label}</small>
              {Array.from({ length: 7 }, (_, k) => {
                const d = new Date(start.getFullYear(), start.getMonth(), start.getDate() + k)
                const c = byDay.get(d.getTime()) ?? 0
                return <i key={k} className={d.getTime() > day(today) ? 'off' : ''} style={c ? { background: 'var(--signal)', opacity: 0.3 + 0.7 * (c / peak) } : undefined} title={`${full.format(d)}: ${c}`} />
              })}
            </div>
          )
        })}
      </div>
      <div className="tk-card">
        <h3>{t('byHour')}</h3>
        {bars(hours, hours.map((_, i) => (i % 3 === 0 ? String(i) : '')))}
      </div>
      <div className="tk-card">
        <h3>{t('byWeekday')}</h3>
        {bars(weekdays, weekdays.map((_, i) => weekday.format(new Date(2024, 0, 1 + i))))}
      </div>
    </div>
  )
}

export function TrackPage(props: {
  matches: TrackMatch[]
  known: Set<string>
  art: Record<string, boolean>
  avatars: Record<string, string>
  faceit: Record<string, FaceitPlayer[]>
  history: EloHistory | null
  icons: boolean
  left: number
  all: number
  onOpen: (path: string) => void
}) {
  const { t, lang, me } = useUi()
  const { matches, known, art, avatars, faceit, history, icons, left, all, onOpen } = props
  const [source, setSource] = useState<Source>('all')
  const [range, setRange] = useState<Range>('all')
  const [map, setMap] = useState('')
  const [pick, setPick] = useState<Key>('rating')
  const [tab, setTab] = useState<Tab>('overview')
  const [versus, setVersus] = useState(0)
  const [stuck, setStuck] = useState(false)
  const main = useRef<HTMLElement>(null)
  const pills = useRef<HTMLElement>(null)
  const [bar, setBar] = useState<{ x: number; w: number } | null>(null)
  useLayoutEffect(() => {
    const nav = pills.current
    if (!nav) return
    const place = () => {
      const on = nav.querySelector<HTMLElement>('button.on')
      if (on) setBar({ x: on.offsetLeft, w: on.offsetWidth })
    }
    place()
    const watch = new ResizeObserver(place)
    nav.querySelectorAll('button').forEach(b => watch.observe(b))
    return () => watch.disconnect()
  }, [tab, lang, matches.length > 0])

  const who = useMemo(() => {
    if (matches.some(m => m.players.some(p => p.steamid === me))) return me
    const seen = new Map<string, number>()
    for (const m of matches) for (const p of m.players) seen.set(p.steamid, (seen.get(p.steamid) ?? 0) + 1)
    return [...seen].sort((a, b) => b[1] - a[1])[0]?.[0] ?? ''
  }, [matches, me])

  const fetched = Object.keys(faceit).length
  useEffect(() => {
    if (who && !history) send({ type: 'faceit_history', steamid: who })
  }, [who, fetched, !history])

  const mine = useMemo(() => {
    const out: Row[] = []
    for (const m of matches) {
      const p = m.players.find(x => x.steamid === who)
      if (!p || (p.team !== 0 && p.team !== 1) || !(p.stats.rounds > 0)) continue
      out.push({ m, s: p.stats, mine: m.score[p.team], theirs: m.score[1 - p.team], source: sourceOf(m.server), cl: p.cl ?? [], wp: p.wp ?? {}, elo: faceit[m.path]?.find(x => x.steamid === who)?.elo || null })
    }
    return out.sort((a, b) => a.m.date - b.m.date)
  }, [matches, who, faceit])

  const name = useMemo(() => {
    const last = mine[mine.length - 1]
    return last?.m.players.find(p => p.steamid === who)?.name ?? ''
  }, [mine, who])

  const sources = useMemo(() => (['faceit', 'valve', 'other'] as Source[]).filter(s => mine.some(r => r.source === s)), [mine])

  const pool = useMemo(() => {
    let out = mine.filter(r => source === 'all' || r.source === source)
    if (range === 'last20') out = out.slice(-20)
    if (range === 'last50') out = out.slice(-50)
    if (range === 'days30') out = out.filter(r => r.m.date * 1000 > Date.now() - 30 * 86400000)
    return out
  }, [mine, source, range])

  const shown = pool.some(r => r.m.map === map) ? map : ''
  const rows = useMemo(() => (shown ? pool.filter(r => r.m.map === shown) : pool), [pool, shown])

  const sum = useMemo(() => total(rows), [rows])
  const eloRows = useMemo(() => {
    if (!history) return []
    const paths = new Map<string, string>()
    for (const p of known) {
      const id = /1-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/i.exec(p)?.[0].toLowerCase()
      if (id) paths.set(id, p)
    }
    let out: Row[] = [...history.items]
      .sort((a, b) => a[0] - b[0])
      .map(([date, elo, , id, map, win, a, b]) => {
        const mine = win ? Math.max(a, b) : Math.min(a, b)
        const theirs = win ? Math.min(a, b) : Math.max(a, b)
        const m = { v: 0, key: id, date, path: paths.get(id) ?? '', map, server: 'faceit', duration: 0, rounds: 0, winners: '', score: [mine, theirs], players: [] } as TrackMatch
        return { m, s: {} as TrackStats, mine, theirs, source: 'faceit' as Source, cl: [], wp: {}, elo }
      })
    if (range === 'last20') out = out.slice(-20)
    if (range === 'last50') out = out.slice(-50)
    if (range === 'days30') out = out.filter(r => r.m.date * 1000 > Date.now() - 30 * 86400000)
    return out
  }, [history, known, range])
  const eloNow = history ? history.elo : sum.elo
  const eloLast = history?.items.length ? history.items.reduce((x, y) => (y[0] > x[0] ? y : x))[2] : null
  const split = rows.length >= 6 ? Math.min(ROLL, Math.floor(rows.length / 2)) : 0
  const late = useMemo(() => total(rows.slice(rows.length - split)), [rows, split])
  const early = useMemo(() => total(rows.slice(0, rows.length - split)), [rows, split])
  const picked = (pick === 'faceitElo' && eloNow === null ? undefined : METRICS.find(m => m.key === pick)) ?? metric('rating')
  const perMap = useMemo(
    () =>
      [...new Set(pool.map(r => r.m.map))]
        .map(id => ({ id, sum: total(pool.filter(r => r.m.map === id)) }))
        .sort((a, b) => b.sum.matches - a.sum.matches),
    [pool],
  )
  const streak = useMemo(() => {
    const last = rows[rows.length - 1]
    if (!last || (!won(last) && !lost(last))) return 0
    let k = 0
    while (k < rows.length && won(rows[rows.length - 1 - k]) === won(last) && lost(rows[rows.length - 1 - k]) === lost(last)) k++
    return won(last) ? k : -k
  }, [rows])
  const sourceName = (s: Source) => (s === 'all' ? t('allSources') : s === 'faceit' ? 'FACEIT' : s === 'valve' ? t('matchmaking') : t('community'))
  const ranges: [Range, string][] = [
    ['all', t('rangeAll')],
    ['last20', t('rangeLast', { n: 20 })],
    ['last50', t('rangeLast', { n: 50 })],
    ['days30', t('rangeDays', { n: 30 })],
  ]
  const tone = (m: Metric, v: number | null) => (v === null || Math.abs(v) < 0.5 * 10 ** -m.digits ? '' : v > 0 !== !!m.low ? ' up' : ' down')
  const count = new Intl.NumberFormat(lang)
  const sided = n(sum, 't_rounds') + n(sum, 'ct_rounds') > 0
  const days = useMemo(() => {
    const out: { key: string; date: number; rows: Row[] }[] = []
    for (const r of rows.slice(-RECENT).reverse()) {
      const key = new Date(r.m.date * 1000).toDateString()
      if (out[out.length - 1]?.key !== key) out.push({ key, date: r.m.date, rows: [] })
      out[out.length - 1].rows.push(r)
    }
    return out
  }, [rows])
  const clutches = useMemo(() => {
    const out: { r: Row; round: number; vs: number; won: boolean; kills: number }[] = []
    for (let i = rows.length - 1; i >= 0; i--) for (const c of rows[i].cl) out.push({ r: rows[i], round: c[0], vs: Math.min(c[1], 5), won: c[2] === 1, kills: c[3] })
    return out
  }, [rows])
  const guns = useMemo(() => {
    const all: Record<string, number> = {}
    for (const r of rows) for (const [w, k] of Object.entries(r.wp)) all[w] = (all[w] ?? 0) + k
    return Object.entries(all).sort((a, b) => b[1] - a[1])
  }, [rows])
  const dayName = new Intl.DateTimeFormat(lang, { weekday: 'long', day: 'numeric', month: 'long' })
  const clock = new Intl.DateTimeFormat(lang, { hour: '2-digit', minute: '2-digit' })
  const tabs: [Tab, Key][] = [
    ['overview', 'tabOverview'],
    ['maps', 'trackMaps'],
    ['details', 'tabDetails'],
    ['matches', 'totalMatches'],
  ]
  const go = (id: Tab) => {
    setTab(id)
    main.current?.scrollTo({ top: 0 })
  }

  const sidesBlock = sided ? (
            <>
              <div className="tk-sides">
                {SIDES.map(([side, label, cls]) => {
                  const rounds = n(sum, `${side}_rounds`)
                  const swing = sum.s[`${side}_swing`] === undefined ? null : per(n(sum, `${side}_swing`), rounds * 100)
                  return (
                    <div key={side} className={`tk-card tk-side ${cls}`}>
                      <h3>
                        <b>{t(label)}</b>
                        {count.format(rounds)} {t('totalRounds')}
                      </h3>
                      <div className="tk-side-win">
                        <b>{whole(sideWon(sum, side))}</b>
                        <span>{t('roundsWon')}</span>
                        <i>
                          <b style={{ width: `${sideWon(sum, side) ?? 0}%` }} />
                        </i>
                      </div>
                      <div className="tk-side-nums">
                        <span>
                          <b>{show(per(n(sum, `${side}_kills`), rounds), metric('kr'), lang)}</b>
                          {t('kr')}
                        </span>
                        <span>
                          <b>{show(per(n(sum, `${side}_damage`), rounds), metric('adr'), lang)}</b>
                          {t('adr')}
                        </span>
                        <span>
                          <b>{show(pct(n(sum, `${side}_kast`), rounds), metric('kast'), lang)}</b>
                          {t('kast')}
                        </span>
                        <span>
                          <b className={`tk-${swingTier(swing)}`}>{swing === null ? '–' : `${swing > 0 ? '+' : ''}${show(swing, metric('roundSwing'), lang)}`}</b>
                          {t('roundSwing')}
                        </span>
                      </div>
                    </div>
                  )
                })}
              </div>
            </>
          ) : null

  return (
    <main ref={main} className="main tk-main" onScroll={e => setStuck(e.currentTarget.scrollTop > 4)}>
      <div className={`tk-head tk-nav${stuck ? ' stuck' : ''}`}>
        <Avatar url={avatars[who]} name={name} />
        <div className="tk-who">
          <h1>{name || t('track')}</h1>
          {rows.length > 0 && (
            <p>
              <span className="tk-won">
                {sum.wins} {t('totalWins')}
              </span>
              <span className="tk-lost">
                {sum.losses} {t('totalLosses')}
              </span>
              {Math.abs(streak) > 1 && <span>{t(streak > 0 ? 'streakWon' : 'streakLost', { n: Math.abs(streak) })}</span>}
            </p>
          )}
        </div>
        {rows.length > 0 && (
          <nav className="tabs tk-tabs" ref={pills}>
            {bar && <span className="tab-bar" style={{ transform: `translateX(${bar.x}px)`, width: bar.w }} />}
            {tabs.map(([id, label]) => (
              <button key={id} className={tab === id ? 'on' : ''} onClick={() => go(id)} aria-pressed={tab === id}>
                {t(label)}
              </button>
            ))}
          </nav>
        )}
        <span className="tk-nav-gap" />
        {left > 0 && (
          <span className="tk-load" role="status">
            <i />
            {t('reading')} {all - left} / {all}
          </span>
        )}
        {shown && (
          <button className="tk-chip" onClick={() => setMap('')}>
            {mapName(shown, t)}
            <svg viewBox="0 0 10 10" aria-hidden="true">
              <path d="M2 2l6 6M8 2l-6 6" />
            </svg>
          </button>
        )}
        {sources.length > 1 && (
          <div className="dt-groups">
            {(['all', ...sources] as Source[]).map(s => (
              <button key={s} className={source === s ? 'on' : ''} onClick={() => setSource(s)}>
                {sourceName(s)}
              </button>
            ))}
          </div>
        )}
        <div className="dt-groups">
          {ranges.map(([r, label]) => (
            <button key={r} className={range === r ? 'on' : ''} onClick={() => setRange(r)}>
              {label}
            </button>
          ))}
        </div>
      </div>

      {rows.length === 0 ? (
        <p className="tk-empty">{t('trackEmpty')}</p>
      ) : (
        <>
          {tab === 'overview' && (
            <>
          <div className="tk-top" style={{ gridTemplateColumns: `repeat(${eloNow !== null ? 7 : 6}, minmax(0, 1fr))` }}>
            {MAIN.filter(key => key !== 'faceitElo' || eloNow !== null).map(key => {
              const m = metric(key)
              const own = key === 'faceitElo' && history !== null
              const v = own ? eloNow : m.of(sum)
              const a = split ? m.of(late) : null
              const b = split ? m.of(early) : null
              const delta = own ? eloLast : a !== null && b !== null ? a - b : null
              const dir = tone(m, delta)
              const tier = key === 'winRate' ? (v === null ? '' : v >= 50 ? 'good' : 'bad') : tierOf(m, v)
              return (
                <button key={key} className={`tk-big${picked.key === key ? ' on' : ''}`} onClick={() => setPick(key)} aria-pressed={picked.key === key}>
                  <span>{t(key)}</span>
                  <b className={`tk-${tier}`}>
                    {key === 'roundSwing' && v !== null && v > 0 ? '+' : ''}
                    {show(v, m, lang)}
                  </b>
                  {delta !== null && dir ? (
                    <small className={`tk-delta${dir}`}>
                      <svg viewBox="0 0 8 8" aria-hidden="true">
                        <path d={delta > 0 ? 'M4 1 7.5 7h-7z' : 'M4 7 .5 1h7z'} />
                      </svg>
                      {show(Math.abs(delta), { ...m, unit: '' }, lang)}
                      <i>{own ? t('lastMatch') : t('rangeLast', { n: split })}</i>
                    </small>
                  ) : (
                    <small className="tk-delta" />
                  )}
                  <Spark values={own ? eloRows.slice(-40).map(r => r.elo) : rolling(rows, m, 5)} />
                </button>
              )
            })}
          </div>

          <div className="tk-form">
            {rows
              .slice(-FORM)
              .reverse()
              .map(r => {
                const v = metric('rating').of(total([r]))
                const open = known.has(r.m.path)
                return (
                  <button key={r.m.key} className={`tk-game${won(r) ? ' won' : lost(r) ? ' lost' : ''}`} disabled={!open} onClick={() => onOpen(r.m.path)} title={open ? undefined : t('metGone')}>
                    {art[r.m.map] && <img src={`https://cs2voice-maps.example/${r.m.map}.png`} alt="" />}
                    <span className="tk-game-map">{mapName(r.m.map, t)}</span>
                    <b>
                      {r.mine}:{r.theirs}
                    </b>
                    <RatingChip value={v} />
                    <small>{when(r.m.date, t, lang)}</small>
                  </button>
                )
              })}
          </div>

          {sidesBlock}

          <div className="tk-board">
            <div className="tk-card tk-plot">
              <Chart rows={picked.key === 'faceitElo' && eloRows.length > 1 ? eloRows : rows} metric={picked} label={t(picked.key)} mean={picked.of(sum)} known={known} onOpen={onOpen} />
            </div>
            <div className="tk-card tk-nums">
              {METRICS.filter(m => !MAIN.includes(m.key)).map(m => {
                const v = m.of(sum)
                const a = split ? m.of(late) : null
                const b = split ? m.of(early) : null
                const delta = a !== null && b !== null ? a - b : null
                const dir = tone(m, delta)
                return (
                  <button key={m.key} className={`tk-num${pick === m.key ? ' on' : ''}`} onClick={() => setPick(m.key)} aria-pressed={pick === m.key}>
                    <span>{t(m.key)}</span>
                    <Spark values={rolling(rows, m, 5)} />
                    {delta !== null && dir && (
                      <small className={`tk-delta${dir}`}>
                        <svg viewBox="0 0 8 8" aria-hidden="true">
                          <path d={delta > 0 ? 'M4 1 7.5 7h-7z' : 'M4 7 .5 1h7z'} />
                        </svg>
                        {show(Math.abs(delta), { ...m, unit: '' }, lang)}
                      </small>
                    )}
                    <b className={`tk-${tierOf(m, v)}`}>{show(v, m, lang)}</b>
                  </button>
                )
              })}
            </div>
          </div>

            </>
          )}

          {tab === 'maps' && (
            <>
          <div className="tk-maps">
            {perMap.map(x => {
              const v = metric('rating').of(x.sum)
              return (
                <button key={x.id} className={`tk-map${shown === x.id ? ' on' : ''}`} onClick={() => setMap(shown === x.id ? '' : x.id)} aria-pressed={shown === x.id}>
                  <span className="tk-map-art">
                    {art[x.id] && <img src={`https://cs2voice-maps.example/${x.id}.png`} alt="" />}
                    <b>{mapName(x.id, t)}</b>
                    <small>{x.sum.matches}</small>
                  </span>
                  <span className="tk-map-line">
                    <span className="tk-won">{x.sum.wins}</span>
                    <i>
                      <b style={{ width: `${pct(x.sum.wins, x.sum.matches) ?? 0}%` }} />
                    </i>
                    <span className="tk-lost">{x.sum.losses}</span>
                  </span>
                  {n(x.sum, 't_rounds') + n(x.sum, 'ct_rounds') > 0 && (
                    <span className="tk-map-sides">
                      {t('roundsWon')}
                      <b className="tk-t">T {whole(sideWon(x.sum, 't'))}</b>
                      <b className="tk-ct">CT {whole(sideWon(x.sum, 'ct'))}</b>
                    </span>
                  )}
                  <span className="tk-map-nums">
                    <span>
                      <b className={`tk-${ratingTier(v)}`}>{show(v, metric('rating'), lang)}</b>
                      {t('rating')}
                    </span>
                    <span>
                      <b>{show(metric('adr').of(x.sum), metric('adr'), lang)}</b>
                      {t('adr')}
                    </span>
                    <span>
                      <b>{show(metric('kd').of(x.sum), metric('kd'), lang)}</b>
                      {t('kd')}
                    </span>
                  </span>
                </button>
              )
            })}
          </div>

            </>
          )}

          {tab === 'details' && (
            <>
              <div className="tk-pair">
                <div>
                  <h2>
                    {t('trackClutches')}
                    <small>
                      {n(sum, 'clutches')} / {n(sum, 'clutch_rounds')}
                    </small>
                  </h2>
                  <div className="tk-clutch">
                    {[1, 2, 3, 4, 5].map(k => {
                      const w = n(sum, `c${k}w`)
                      const l = n(sum, `c${k}l`)
                      return (
                        <button key={k} className={`tk-card${w + l ? '' : ' none'}${versus === k ? ' on' : ''}`} onClick={() => setVersus(versus === k ? 0 : k)} aria-pressed={versus === k} disabled={!(w + l)}>
                          <span>1v{k}</span>
                          <b>
                            {w}
                            <small>/ {w + l}</small>
                          </b>
                          <i>
                            <b style={{ width: `${pct(w, w + l) ?? 0}%` }} />
                          </i>
                          <em>{w + l ? whole(pct(w, w + l)) : '–'}</em>
                        </button>
                      )
                    })}
                  </div>
                  {clutches.length > 0 && (
                    <div className="tk-card tk-plays">
                      {clutches
                        .filter(c => !versus || c.vs === versus)
                        .map(c => {
                          const open = known.has(c.r.m.path)
                          return (
                            <button key={`${c.r.m.key}-${c.round}`} className="tk-play" disabled={!open} onClick={() => onOpen(c.r.m.path)} title={open ? undefined : t('metGone')}>
                              <span className="tk-thumb">{art[c.r.m.map] && <img src={`https://cs2voice-maps.example/${c.r.m.map}.png`} alt="" loading="lazy" />}</span>
                              <b className={c.won ? 'tk-won' : 'tk-lost'}>1v{c.vs}</b>
                              <span className="tk-play-map">
                                <b>{mapName(c.r.m.map, t)}</b>
                                <small>
                                  {t('roundN', { n: c.round + 1 })} · {when(c.r.m.date, t, lang)}
                                </small>
                              </span>
                              <span className="tk-play-kills">
                                {Array.from({ length: Math.min(c.kills, 5) }, (_, k) => (
                                  <Icon key={k} name="icons_ui_kill" ready={icons} />
                                ))}
                                {!icons && c.kills > 0 && <em>{c.kills} K</em>}
                              </span>
                              <span className={c.r.mine > c.r.theirs ? 'tk-won' : c.r.mine < c.r.theirs ? 'tk-lost' : ''}>
                                {c.r.mine}:{c.r.theirs}
                              </span>
                            </button>
                          )
                        })}
                    </div>
                  )}
                </div>
                <div>
                  <h2>{t('trackWeapons')}</h2>
                  <div className={`tk-card tk-guns${icons ? ' ic' : ''}`}>
                    {(guns.length
                      ? guns.map(([w, kills]) => ({ id: w, name: weaponName(w), icon: w, kills }))
                      : WEAPONS.filter(([, field]) => n(sum, field) > 0).map(([key, field, icon]) => ({ id: field, name: t(key), icon, kills: n(sum, field) }))
                    ).map((g, _, list) => (
                      <div key={g.id} className="tk-gun">
                        <span className="tk-gun-ic">
                          <Icon name={`icons_equipment_${g.icon}`} ready={icons} />
                        </span>
                        <span>{g.name}</span>
                        <i>
                          <b style={{ width: `${(100 * g.kills) / Math.max(1, list[0].kills)}%` }} />
                        </i>
                        <b>{count.format(g.kills)}</b>
                        <em>{Math.round(pct(g.kills, n(sum, 'kills')) ?? 0)}%</em>
                      </div>
                    ))}
                  </div>
                </div>
              </div>

              <div className="tk-groups">
                {GROUPS.map(([title, list]) => (
                  <div key={title}>
                    <h2>{t(title)}</h2>
                    <div className="tk-card tk-totals">
                      {list.map(i => (
                        <div key={i}>
                          <b>{count.format(TOTALS[i][1](sum))}</b>
                          <span>{t(TOTALS[i][0])}</span>
                        </div>
                      ))}
                    </div>
                  </div>
                ))}
              </div>

              <h2>{t('trackActivity')}</h2>
              <Activity rows={rows} />
            </>
          )}

          {tab === 'matches' && (
            <div className="tk-days">
              <div className="tk-match tk-th">
                <span />
                <span />
                <span />
                <span>K / D / A</span>
                <span>{t('colAdr')}</span>
                <span>{t('colRating')}</span>
                <span>{t('colImpact')}</span>
                <span>{t('colSwing')}</span>
              </div>
              {days.map(d => (
                <section key={d.key}>
                  <h3>
                    <b>{dayName.format(new Date(d.date * 1000))}</b>
                    <span className="tk-won">{d.rows.filter(won).length}</span>
                    <span className="tk-lost">{d.rows.filter(lost).length}</span>
                  </h3>
                  {d.rows.map(r => {
                    const one = total([r])
                    const open = known.has(r.m.path)
                    const swing = metric('roundSwing').of(one)
                    return (
                      <button key={r.m.key} className="tk-match" disabled={!open} onClick={() => onOpen(r.m.path)} title={open ? undefined : t('metGone')}>
                        <span className="tk-thumb">{art[r.m.map] && <img src={`https://cs2voice-maps.example/${r.m.map}.png`} alt="" loading="lazy" />}</span>
                        <span className="tk-match-map">
                          <b>{mapName(r.m.map, t)}</b>
                          <small>
                            {clock.format(new Date(r.m.date * 1000))}
                            {r.source === 'faceit' ? ' · FACEIT' : ''}
                          </small>
                        </span>
                        <span className={`tk-match-score ${won(r) ? 'tk-won' : lost(r) ? 'tk-lost' : ''}`}>
                          {r.mine}:{r.theirs}
                        </span>
                        <span>
                          {r.s.kills} / {r.s.deaths} / {r.s.assists}
                        </span>
                        <span>{show(metric('adr').of(one), metric('adr'), lang)}</span>
                        <span>
                          <RatingChip value={metric('rating').of(one)} />
                        </span>
                        <span className={`tk-${ratingTier(metric('impact').of(one))}`}>{show(metric('impact').of(one), metric('impact'), lang)}</span>
                        <span className={`tk-${swingTier(swing)}`}>{swing === null ? '–' : `${swing > 0 ? '+' : ''}${show(swing, metric('roundSwing'), lang)}`}</span>
                      </button>
                    )
                  })}
                </section>
              ))}
            </div>
          )}
        </>
      )}
    </main>
  )
}
