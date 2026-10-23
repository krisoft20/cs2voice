import type { Player, Roster } from './bridge'
import { useUi } from './common'
import { rating } from './stats'
import { teamLabels } from './voice'

const LINE_IN = 0
const LINE_SIDE = 4

export const winnerTeam = (roster: Roster, round: number) => {
  const side = roster.winners[round]
  for (const p of roster.players) {
    const l = p.stats?.lines[round]
    if (l && l[LINE_IN] && l[LINE_SIDE] === side) return p.team
  }
  return -1
}

export const sideOf = (roster: Roster, team: number, round: number) => {
  for (const p of roster.players) {
    const l = p.stats?.lines[round]
    if (p.team === team && l && l[LINE_IN]) return l[LINE_SIDE]
  }
  return 0
}

type Row = { label: Parameters<ReturnType<typeof useUi>['t']>[0]; value: (ps: Player[]) => number; digits: number }

const sum = (ps: Player[], f: (p: Player) => number) => ps.reduce((n, p) => n + f(p), 0)
const avg = (ps: Player[], f: (p: Player) => number) => (ps.length ? sum(ps, f) / ps.length : 0)

const ROWS: Row[] = [
  { label: 'colKills', value: ps => sum(ps, p => p.stats!.kills), digits: 0 },
  { label: 'colAdr', value: ps => avg(ps, p => p.stats!.damage / Math.max(p.stats!.rounds, 1)), digits: 1 },
  { label: 'colKast', value: ps => avg(ps, p => (100 * p.stats!.kast) / Math.max(p.stats!.rounds, 1)), digits: 0 },
  { label: 'colEntryKills', value: ps => sum(ps, p => p.stats!.entry_kills), digits: 0 },
  { label: 'colTrades', value: ps => sum(ps, p => p.stats!.trade_kills), digits: 0 },
  { label: 'colClutch', value: ps => sum(ps, p => p.stats!.clutches), digits: 0 },
  { label: 'colUtil', value: ps => sum(ps, p => p.stats!.utility_damage), digits: 0 },
  { label: 'colFlashed', value: ps => sum(ps, p => p.stats!.enemies_flashed), digits: 0 },
]

export function TeamCompare(props: { roster: Roster }) {
  const { t, me } = useUi()
  const { roster } = props
  const labels = teamLabels(roster, me, t)
  const teams = [0, 1].map(team => roster.players.filter(p => p.team === team && p.stats && p.stats.rounds > 0))
  const sides = [0, 1].map(i => (roster.teams[i]?.side === 3 ? 'ct' : 't'))
  const best = [...roster.players].filter(p => p.stats && p.stats.rounds > 0).sort((a, b) => (rating(b.stats!) ?? 0) - (rating(a.stats!) ?? 0))[0]
  return (
    <section className="compare">
      <div className="compare-head">
        <span className={sides[0]}>{labels[0]}</span>
        <span className={sides[1]}>{labels[1]}</span>
      </div>
      {ROWS.map(row => {
        const a = row.value(teams[0])
        const b = row.value(teams[1])
        const total = a + b || 1
        return (
          <div key={row.label} className="compare-row">
            <span className="cv">{a.toFixed(row.digits)}</span>
            <div className="cbar">
              <span className={sides[0]} style={{ width: `${(a / total) * 100}%` }} />
              <span className={sides[1]} style={{ width: `${(b / total) * 100}%` }} />
              <em>{t(row.label)}</em>
            </div>
            <span className="cv">{b.toFixed(row.digits)}</span>
          </div>
        )
      })}
      {best && (
        <div className="mvp">
          <span className="mvp-label">{t('bestPlayer')}</span>
          <strong className={best.team === 0 ? sides[0] : sides[1]}>{best.name}</strong>
          <span>{rating(best.stats!)?.toFixed(2)}</span>
        </div>
      )}
    </section>
  )
}
