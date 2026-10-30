import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import type { HistoryMatch, Roster } from './bridge'
import { signature } from './cleanplan'
import { useUi } from './common'
import { SwordsIcon, UsersIcon } from './icons'
import { mapName, when } from './voice'

export type Meeting = { key: string; date: number; map: string; mine: number; theirs: number; together: boolean; name: string; kda: [number, number, number] }

const MAX_ROWS = 30

export const meetings = (history: Record<string, HistoryMatch>, roster: Roster | undefined, me: string) => {
  const out: Record<string, Meeting[]> = {}
  if (!roster || !me || me === '0') return out
  const here = signature(roster)
  const ids = new Set(roster.players.map(p => p.steamid))
  for (const m of Object.values(history)) {
    if (m.key === here) continue
    const self = m.players.find(p => p[0] === me)
    if (!self || (self[2] !== 0 && self[2] !== 1)) continue
    for (const p of m.players) {
      if (p[0] === me || !ids.has(p[0]) || (p[2] !== 0 && p[2] !== 1)) continue
      ;(out[p[0]] ??= []).push({
        key: m.key,
        date: m.date,
        map: m.map,
        mine: m.score[self[2]],
        theirs: m.score[1 - self[2]],
        together: p[2] === self[2],
        name: p[1],
        kda: [p[3], p[4], p[5]],
      })
    }
  }
  for (const list of Object.values(out)) list.sort((a, b) => b.date - a.date)
  return out
}

export function Met(props: { list?: Meeting[]; name: string; has: (key: string) => boolean; open: (key: string) => void }) {
  const { t, lang } = useUi()
  const [at, setAt] = useState<{ x: number; y: number; up: boolean } | null>(null)
  const chip = useRef<HTMLSpanElement>(null)
  const pop = useRef<HTMLDivElement>(null)
  const list = props.list ?? []
  const together = list.filter(m => m.together).length
  const against = list.length - together

  useEffect(() => {
    if (!at) return
    const down = (e: MouseEvent) => !pop.current?.contains(e.target as Node) && !chip.current?.contains(e.target as Node) && setAt(null)
    const key = (e: KeyboardEvent) => e.key === 'Escape' && setAt(null)
    const away = () => setAt(null)
    window.addEventListener('mousedown', down)
    window.addEventListener('keydown', key)
    window.addEventListener('resize', away)
    document.addEventListener('scroll', away, true)
    return () => {
      window.removeEventListener('mousedown', down)
      window.removeEventListener('keydown', key)
      window.removeEventListener('resize', away)
      document.removeEventListener('scroll', away, true)
    }
  }, [at])

  useLayoutEffect(() => {
    if (!at || !pop.current) return
    const r = pop.current.getBoundingClientRect()
    const x = Math.max(8, Math.min(at.x, window.innerWidth - r.width - 8))
    if (x !== at.x) setAt({ ...at, x })
  }, [at])

  if (!list.length) return null

  const toggle = (e: React.SyntheticEvent) => {
    e.stopPropagation()
    e.preventDefault()
    if (at) return setAt(null)
    const r = chip.current?.getBoundingClientRect()
    if (!r) return
    const up = r.bottom + 360 > window.innerHeight && r.top > window.innerHeight / 2
    setAt({ x: r.left, y: up ? window.innerHeight - r.top + 6 : r.bottom + 6, up })
  }

  return (
    <>
      <span
        ref={chip}
        className={`met${at ? ' on' : ''}`}
        role="button"
        tabIndex={0}
        aria-expanded={!!at}
        title={[together ? t('metTogetherTip', { n: together }) : '', against ? t('metAgainstTip', { n: against }) : ''].filter(Boolean).join('\n')}
        onClick={toggle}
        onKeyDown={e => (e.key === 'Enter' || e.key === ' ') && toggle(e)}
      >
        {together > 0 && (
          <span className="met-part together">
            <UsersIcon />
            {together}
          </span>
        )}
        {against > 0 && (
          <span className="met-part against">
            <SwordsIcon />
            {against}
          </span>
        )}
      </span>
      {at &&
        createPortal(
          <div
            ref={pop}
            className="met-pop"
            role="dialog"
            aria-label={props.name}
            style={at.up ? { left: at.x, bottom: at.y } : { left: at.x, top: at.y }}
          >
            <header>
              <strong>{props.name}</strong>
              <span>{[together ? t('metCountTogether', { n: together }) : '', against ? t('metCountAgainst', { n: against }) : ''].filter(Boolean).join(', ')}</span>
            </header>
            <ul>
              {list.slice(0, MAX_ROWS).map(m => {
                const res = m.mine > m.theirs ? 'won' : m.mine < m.theirs ? 'lost' : 'tie'
                return (
                  <li key={m.key}>
                    <button
                      disabled={!props.has(m.key)}
                      title={props.has(m.key) ? undefined : t('metGone')}
                      onClick={() => {
                        setAt(null)
                        props.open(m.key)
                      }}
                    >
                      <span className={`met-score ${res}`} title={t(res === 'won' ? 'metWon' : res === 'lost' ? 'metLost' : 'metTie')}>
                        {m.mine}:{m.theirs}
                      </span>
                      <span className="met-map">{mapName(m.map, t)}</span>
                      <span className={`met-side ${m.together ? 'together' : 'against'}`}>{t(m.together ? 'metTogether' : 'metAgainst')}</span>
                      <span className="met-kda">
                        {m.kda[0]}/{m.kda[1]}/{m.kda[2]}
                      </span>
                      <span className="met-when">{when(m.date, t, lang)}</span>
                      {m.name && m.name !== props.name && <span className="met-as">{t('metAs', { name: m.name })}</span>}
                    </button>
                  </li>
                )
              })}
            </ul>
          </div>,
          document.body,
        )}
    </>
  )
}
