import { useEffect, useMemo, useState } from 'react'
import type { Player, Roster, Stats } from './bridge'
import { Avatar, remember, remembered, useUi } from './common'
import type { Key } from './i18n'
import { pct, per, rating } from './stats'
import { TeamCompare, sideOf, winnerTeam } from './Summary'
import { teamLabels } from './voice'

type Column = { key: string; label: Key; group: Key; value: (s: Stats) => number | null; show: (s: Stats) => string; low?: boolean }

const num = (v: number | null, digits = 0) => (v === null ? '–' : v.toFixed(digits))
const money = (v: number) => `$${Math.round(v).toLocaleString('en-US')}`
const ecoSum = (s: Stats, k: number) => (s.eco ?? []).reduce((n, e) => n + e[k], 0)

const COLUMNS: Column[] = [
  { key: 'k', label: 'colKills', group: 'groupFrags', value: s => s.kills, show: s => `${s.kills}` },
  { key: 'd', label: 'colDeaths', group: 'groupFrags', value: s => s.deaths, show: s => `${s.deaths}`, low: true },
  { key: 'a', label: 'colAssists', group: 'groupFrags', value: s => s.assists, show: s => `${s.assists}` },
  { key: 'diff', label: 'colDiff', group: 'groupFrags', value: s => s.kills - s.deaths, show: s => `${s.kills - s.deaths > 0 ? '+' : ''}${s.kills - s.deaths}` },
  { key: 'rating', label: 'colRating', group: 'groupFrags', value: rating, show: s => num(rating(s), 2) },
  { key: 'adr', label: 'colAdr', group: 'groupFrags', value: s => per(s.damage, s.rounds), show: s => num(per(s.damage, s.rounds), 1) },
  { key: 'kast', label: 'colKast', group: 'groupFrags', value: s => pct(s.kast, s.rounds), show: s => num(pct(s.kast, s.rounds), 0) },
  { key: 'hs', label: 'colHs', group: 'groupAim', value: s => pct(s.hs_kills, s.kills), show: s => num(pct(s.hs_kills, s.kills), 0) },
  { key: 'acc', label: 'colAcc', group: 'groupAim', value: s => pct(s.hits, s.shots), show: s => num(pct(s.hits, s.shots), 1) },
  { key: 'head', label: 'colHead', group: 'groupAim', value: s => pct(s.head_hits, s.hits), show: s => num(pct(s.head_hits, s.hits), 1) },
  { key: 'wb', label: 'colWallbang', group: 'groupAim', value: s => s.wallbang_kills, show: s => `${s.wallbang_kills}` },
  { key: 'smoke', label: 'colSmoke', group: 'groupAim', value: s => s.smoke_kills, show: s => `${s.smoke_kills}` },
  { key: 'ek', label: 'colEntryKills', group: 'groupOpening', value: s => s.entry_kills, show: s => `${s.entry_kills}` },
  { key: 'ed', label: 'colEntryDeaths', group: 'groupOpening', value: s => s.entry_deaths, show: s => `${s.entry_deaths}`, low: true },
  { key: 'trade', label: 'colTrades', group: 'groupOpening', value: s => s.trade_kills, show: s => `${s.trade_kills}` },
  { key: 'clutch', label: 'colClutch', group: 'groupOpening', value: s => s.clutches, show: s => `${s.clutches}/${s.clutch_rounds}` },
  { key: 'k2', label: 'col2k', group: 'groupOpening', value: s => s.k2, show: s => `${s.k2}` },
  { key: 'k3', label: 'col3k', group: 'groupOpening', value: s => s.k3, show: s => `${s.k3}` },
  { key: 'k4', label: 'col4k', group: 'groupOpening', value: s => s.k4, show: s => `${s.k4}` },
  { key: 'k5', label: 'col5k', group: 'groupOpening', value: s => s.k5, show: s => `${s.k5}` },
  { key: 'ud', label: 'colUtil', group: 'groupUtility', value: s => s.utility_damage, show: s => `${s.utility_damage}` },
  { key: 'ef', label: 'colFlashed', group: 'groupUtility', value: s => s.enemies_flashed, show: s => `${s.enemies_flashed}` },
  { key: 'fa', label: 'colFlashAssists', group: 'groupUtility', value: s => s.flash_assists, show: s => `${s.flash_assists}` },
  { key: 'gear', label: 'colGear', group: 'groupEconomy', value: s => per(ecoSum(s, 0), s.eco?.length ?? 0), show: s => { const v = per(ecoSum(s, 0), s.eco?.length ?? 0); return v === null ? '–' : money(v) } },
  { key: 'earned', label: 'colEarned', group: 'groupEconomy', value: s => ecoSum(s, 2), show: s => money(ecoSum(s, 2)) },
  { key: 'saved', label: 'colSaved', group: 'groupEconomy', value: s => ecoSum(s, 1), show: s => money(ecoSum(s, 1)) },
  { key: 'live', label: 'colAlive', group: 'groupEconomy', value: s => per(ecoSum(s, 3), s.eco?.length ?? 0), show: s => { const v = per(ecoSum(s, 3), s.eco?.length ?? 0); return v === null ? '–' : `${Math.round(v)} s` } },
]

const GROUPS: Key[] = ['groupFrags', 'groupAim', 'groupOpening', 'groupUtility', 'groupEconomy']

const ICONS = 'https://cs2voice-maps.example/icons/'
const FLAGS: [number, string][] = [
  [1, 'hud_deathnotice_icon_headshot'],
  [2, 'hud_deathnotice_penetrate'],
  [4, 'hud_deathnotice_smoke_kill'],
  [8, 'hud_deathnotice_blind_kill'],
  [16, 'hud_deathnotice_noscope'],
]

export const reasonOf = (r: number): { icon: string; label: Key } => {
  if (r === 1) return { icon: 'icons_ui_bomb', label: 'reasonBomb' }
  if (r === 7) return { icon: 'icons_ui_defuser', label: 'reasonDefuse' }
  if (r === 12) return { icon: 'icons_ui_clock', label: 'reasonTime' }
  if (r === 17 || r === 18) return { icon: 'icons_ui_kill', label: 'reasonSurrender' }
  return { icon: 'icons_ui_kill', label: 'reasonElimination' }
}

export function Icon(props: { name: string; ready: boolean; className?: string; alt?: string }) {
  const { name, ready, className, alt } = props
  const [bad, setBad] = useState(false)
  useEffect(() => setBad(false), [name, ready])
  if (!ready || bad) return null
  return <img className={className} src={`${ICONS}${name}.svg`} alt={alt ?? ''} onError={() => setBad(true)} />
}

const roundedClock = (secs: number) => {
  const s = Math.max(0, Math.round(secs))
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}

export function Strip(props: { roster: Roster; labels: string[]; pick: number | null; onPick: (r: number) => void; icons: boolean; heat?: number[]; heatLabel?: string }) {
  const { roster, labels, pick, onPick, icons, heat, heatLabel } = props
  const count = roster.winners.length
  const info = roster.round_info ?? []
  return (
    <div className="dt-strip">
      <div className={`dt-names${heat ? ' with-heat' : ''}`}>
        {[0, 1].map(i => (
          <span key={i} className={sideOf(roster, i, 0) === 3 ? 'ct' : 't'}>
            {labels[i]}
          </span>
        ))}
        {heat && <small>{heatLabel}</small>}
      </div>
      <div className="dt-rounds">
        {Array.from({ length: count }, (_, r) => {
          const w = winnerTeam(roster, r)
          const side = w >= 0 ? sideOf(roster, w, r) : 0
          const swap = r > 0 && sideOf(roster, 0, r) !== sideOf(roster, 0, r - 1)
          const reason = reasonOf(info[r]?.reason ?? 0)
          const cell = (team: number) =>
            w === team ? (
              <span className={`won ${side === 3 ? 'ct' : 't'}`}>
                <Icon name={reason.icon} ready={icons} />
              </span>
            ) : (
              <span />
            )
          return (
            <button key={r} className={`dt-rd${swap ? ' swap' : ''}${pick === r ? ' on' : ''}${heat ? ' with-heat' : ''}`} onClick={() => onPick(r)}>
              {cell(0)}
              {cell(1)}
              {heat && (
                <i className="dt-heat">
                  <b style={{ height: `${Math.round(Math.max(0, Math.min(1, heat[r] ?? 0)) * 100)}%` }} />
                </i>
              )}
              <small>{r + 1}</small>
            </button>
          )
        })}
      </div>
    </div>
  )
}

type Event = { tick: number; kind: 'kill'; k: [number, number, number, number, number, string] } | { tick: number; kind: 'bomb'; b: [number, number, number] }

function RoundView(props: { roster: Roster; labels: string[]; avatars: Record<string, string>; round: number; icons: boolean; from: number }) {
  const { t } = useUi()
  const { roster, labels, avatars, round, icons, from } = props
  const info = roster.round_info?.[round]
  const ti = roster.tick_interval || 1 / 64
  const bySlot = useMemo(() => new Map(roster.players.map(p => [p.slot, p])), [roster])
  const sideAt = (p: Player | undefined) => p?.stats?.lines[round]?.[4] ?? 0
  const w = winnerTeam(roster, round)
  const reason = reasonOf(info?.reason ?? 0)
  const score = [0, 1].map(team => roster.winners.slice(0, round + 1).filter((_, r) => winnerTeam(roster, r) === team).length)
  const players = (team: number) =>
    roster.players
      .filter(p => p.team === team && p.stats?.lines[round]?.[0])
      .sort((a, b) => b.stats!.lines[round][3] - a.stats!.lines[round][3])
  const gear = (team: number) => players(team).reduce((n, p) => n + (p.stats!.eco?.[round]?.[0] ?? 0), 0)
  const gearMax = useMemo(() => {
    let m = 1
    for (let r = 0; r < roster.winners.length; r++)
      for (const team of [0, 1]) {
        let n = 0
        for (const p of roster.players) if (p.team === team && p.stats?.lines[r]?.[0]) n += p.stats.eco?.[r]?.[0] ?? 0
        m = Math.max(m, n)
      }
    return m
  }, [roster])
  const events: Event[] = [
    ...(info?.kills ?? []).map(k => ({ tick: k[0], kind: 'kill' as const, k })),
    ...(info?.bomb ?? []).map(b => ({ tick: b[0], kind: 'bomb' as const, b })),
  ].sort((a, b) => a.tick - b.tick)
  const start = info?.start ?? 0
  const long = info && info.decided > start ? (info.decided - start) * ti : null
  const name = (slot: number) => bySlot.get(slot)?.name ?? t('slot', { n: slot })
  const cls = (slot: number) => (sideAt(bySlot.get(slot)) === 3 ? 'ct' : 't')
  return (
    <div key={round} className={`dt-body dt-in${from > round ? ' back' : from < round ? ' next' : ''}`}>
      <section className="dt-round">
        <div className="dt-head">
          <h2>{t('roundN', { n: round + 1 })}</h2>
          {w >= 0 && (
            <span className={`dt-won ${sideOf(roster, w, round) === 3 ? 'ct' : 't'}`}>
              <Icon name={reason.icon} ready={icons} />
              {t('roundWon', { team: labels[w], how: t(reason.label) })}
            </span>
          )}
          {long !== null && <span className="dt-meta">{t('roundLong', { t: roundedClock(long) })}</span>}
          <span className="dt-score">
            <span className={sideOf(roster, 0, round) === 3 ? 'ct' : 't'}>{score[0]}</span>:<span className={sideOf(roster, 1, round) === 3 ? 'ct' : 't'}>{score[1]}</span>
          </span>
        </div>
        <div className="dt-eco">
          {[0, 1].map(team => (
            <div key={team} className={`dt-ecorow ${sideOf(roster, team, round) === 3 ? 'ct' : 't'}`}>
              <b>{labels[team]}</b>
              <div className="dt-bar">
                <i style={{ width: `${(gear(team) / gearMax) * 100}%` }} />
              </div>
              <span>{t('gearValue', { v: money(gear(team)) })}</span>
            </div>
          ))}
        </div>
        <div className="dt-label">{t('roundFeed')}</div>
        <div className="dt-feed">
          {events.map((e, i) =>
            e.kind === 'kill' ? (
              <div key={i} className="dt-kill">
                <span className="dt-time">{roundedClock((e.tick - start) * ti)}</span>
                <span className={`dt-a ${cls(e.k[1])}`}>
                  {e.k[1] >= 0 && e.k[1] !== e.k[2] ? name(e.k[1]) : ''}
                  {e.k[3] >= 0 && <small className={cls(e.k[3])}> + {name(e.k[3])}</small>}
                </span>
                <span className="dt-gun">
                  <Icon name={`icons_equipment_${e.k[5]}`} ready={icons} className="dt-weapon" alt={e.k[5]} />
                  {FLAGS.filter(([f]) => e.k[4] & f).map(([f, n]) => (
                    <Icon key={f} name={n} ready={icons} className="dt-flag" />
                  ))}
                </span>
                <span className={`dt-v ${cls(e.k[2])}`}>{name(e.k[2])}</span>
                <span className="dt-tags">
                  {e.k[4] & 64 ? <em className="open">{t('tagOpening')}</em> : null}
                  {e.k[4] & 128 ? <em>{t('tagTrade')}</em> : null}
                </span>
              </div>
            ) : (
              <div key={i} className="dt-bomb">
                <span className="dt-time">{roundedClock((e.tick - start) * ti)}</span>
                <span>
                  <Icon name={e.b[2] === 0 ? 'icons_equipment_c4' : e.b[2] === 1 ? 'icons_equipment_defuser' : 'icons_ui_bomb'} ready={icons} className="dt-bomb-ic" />
                  {e.b[2] === 2 ? (
                    t('bombExploded')
                  ) : (
                    <>
                      <b className={cls(e.b[1])}>{name(e.b[1])}</b> {t(e.b[2] === 0 ? 'bombPlanted' : 'bombDefused')}
                    </>
                  )}
                </span>
              </div>
            ),
          )}
          {events.length === 0 && <div className="dt-none">{t('roundQuiet')}</div>}
        </div>
      </section>
      <aside className="dt-board">
        <div className="dt-prow dt-phead">
          <span>{t('roundN', { n: round + 1 })}</span>
          <span>{t('colGear')}</span>
          <span>{t('colDmg')}</span>
          <span>{t('colKills')}</span>
        </div>
        {[0, 1].map(team => (
          <div key={team} className="dt-team">
            {players(team).map(p => {
              const l = p.stats!.lines[round]
              const e = p.stats!.eco?.[round]
              return (
                <div key={p.slot} className={`dt-prow ${l[4] === 3 ? 'ct' : 't'}${l[2] ? ' dead' : ''}`}>
                  <span className="dt-name">
                    <Avatar url={avatars[p.steamid]} name={p.name} />
                    <span>{p.name}</span>
                  </span>
                  <span className="dt-money">{e ? money(e[0]) : '–'}</span>
                  <span>{l[3]}</span>
                  <span className="dt-ks">
                    {Array.from({ length: Math.min(l[1], 5) }, (_, k) => (
                      <Icon key={k} name="icons_ui_kill" ready={icons} />
                    ))}
                  </span>
                </div>
              )
            })}
          </div>
        ))}
      </aside>
    </div>
  )
}

function MatchView(props: { roster: Roster; avatars: Record<string, string> }) {
  const { t } = useUi()
  const { roster, avatars } = props
  const [group, setGroupState] = useState<Key>(() => {
    const g = remembered('cs2voice.details.group', 'groupFrags') as Key
    return GROUPS.includes(g) ? g : 'groupFrags'
  })
  const setGroup = (g: Key) => {
    setGroupState(g)
    remember('cs2voice.details.group', g)
  }
  const [sort, setSort] = useState<{ key: string; up: boolean } | null>(null)
  const cols = COLUMNS.filter(c => c.group === group)
  const col = COLUMNS.find(c => c.key === sort?.key)
  const players = roster.players.filter(p => p.stats && p.stats.rounds > 0 && (p.team === 0 || p.team === 1))
  const rows = [...players].sort((a, b) => {
    if (col) {
      const va = col.value(a.stats!) ?? -Infinity
      const vb = col.value(b.stats!) ?? -Infinity
      return sort!.up ? va - vb : vb - va
    }
    return (rating(b.stats!) ?? 0) - (rating(a.stats!) ?? 0)
  })
  const best = (c: Column) => {
    const vals = players.map(p => c.value(p.stats!)).filter((v): v is number => v !== null)
    return c.low ? Math.min(...vals) : Math.max(...vals)
  }
  return (
    <div className="dt-body dt-match dt-fade">
      <TeamCompare roster={roster} />
      <section className="dt-table">
        <div className="dt-groups" role="tablist">
          {GROUPS.map(g => (
            <button key={g} role="tab" aria-selected={group === g} className={group === g ? 'on' : ''} onClick={() => setGroup(g)}>
              {t(g)}
            </button>
          ))}
        </div>
        <table key={group} className="dt-swap">
          <thead>
            <tr>
              <th className="who">{t('colPlayer')}</th>
              {cols.map(c => (
                <th
                  key={c.key}
                  className={`sortable${sort?.key === c.key ? ' sorted' : ''}`}
                  onClick={() => setSort(s => (s?.key === c.key ? (s.up ? null : { key: c.key, up: true }) : { key: c.key, up: false }))}
                >
                  {t(c.label)}
                  {sort?.key === c.key && <span className="arrow">{sort.up ? '▲' : '▼'}</span>}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {rows.map(p => (
              <tr key={p.slot} className={roster.teams[p.team]?.side === 3 ? 'ct' : 't'}>
                <td className="who">
                  <Avatar url={avatars[p.steamid]} name={p.name} />
                  <span>{p.name}</span>
                </td>
                {cols.map(c => {
                  const v = c.value(p.stats!)
                  const top = v !== null && players.length > 1 && v === best(c) && (c.low || v > 0)
                  return (
                    <td key={c.key} className={top ? 'top' : ''}>
                      {c.show(p.stats!)}
                    </td>
                  )
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </section>
    </div>
  )
}

export function Details(props: { roster: Roster; avatars: Record<string, string>; icons: boolean }) {
  const { t, me } = useUi()
  const { roster, avatars, icons } = props
  const labels = teamLabels(roster, me, t)
  const [view, setViewState] = useState<'rounds' | 'match'>(() => (remembered('cs2voice.details.view', 'rounds') === 'match' ? 'match' : 'rounds'))
  const setView = (v: 'rounds' | 'match') => {
    setViewState(v)
    remember('cs2voice.details.view', v)
  }
  const [from, setFrom] = useState(0)
  const [pick, setPick] = useState(0)
  const count = roster.winners.length
  const round = Math.min(pick, Math.max(0, count - 1))
  const go = (r: number) => {
    setFrom(round)
    setPick(r)
  }
  useEffect(() => {
    setPick(0)
    setFrom(0)
  }, [roster.path])
  useEffect(() => {
    if (view !== 'rounds') return
    const key = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement) return
      if (e.key === 'ArrowLeft' && round > 0) go(round - 1)
      else if (e.key === 'ArrowRight' && round < count - 1) go(round + 1)
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [view, count, round])
  return (
    <div className="dtl">
      <div className="dt-top">
        <Strip
          roster={roster}
          labels={labels}
          pick={view === 'rounds' ? round : null}
          onPick={r => {
            if (view === 'rounds') go(r)
            else {
              setFrom(r)
              setPick(r)
            }
            setView('rounds')
          }}
          icons={icons}
        />
        <div className="dt-views" role="tablist">
          <button role="tab" aria-selected={view === 'rounds'} className={view === 'rounds' ? 'on' : ''} onClick={() => setView('rounds')}>
            {t('viewRounds')}
          </button>
          <button role="tab" aria-selected={view === 'match'} className={view === 'match' ? 'on' : ''} onClick={() => setView('match')}>
            {t('viewMatch')}
          </button>
        </div>
      </div>
      {view === 'rounds' && count > 0 ? (
        <RoundView roster={roster} labels={labels} avatars={avatars} round={round} icons={icons} from={from} />
      ) : (
        <MatchView roster={roster} avatars={avatars} />
      )}
    </div>
  )
}
