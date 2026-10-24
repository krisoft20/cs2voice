import { useEffect, useState } from 'react'
import { send, type Demo, type Roster } from './bridge'
import { useUi } from './common'
import { mapName, source, teamLabels, when } from './voice'

type Menu = { x: number; y: number; paths: string[]; path: string }

export function DemoList(props: {
  demos: Demo[] | null
  rosters: Record<string, Roster>
  art: Record<string, boolean>
  selected: string | null
  onSelect: (p: string) => void
  query: string
}) {
  const { t, lang, me } = useUi()
  const { demos: all, rosters, art, selected, onSelect, query } = props
  const [picked, setPicked] = useState<Set<string>>(new Set())
  const [anchor, setAnchor] = useState<string | null>(null)
  const [menu, setMenu] = useState<Menu | null>(null)
  const [confirm, setConfirm] = useState<string[] | null>(null)

  const words = query.toLowerCase().split(/\s+/).filter(Boolean)
  const demos = !all || !words.length ? all : all.filter(d => {
    const r = rosters[d.path]
    const hay = [d.name, r?.map ?? '', r ? mapName(r.map, t) : '', ...(r ? teamLabels(r, me, t) : []), ...(r?.players.map(p => p.name) ?? [])].join('\n').toLowerCase()
    return words.every(w => hay.includes(w))
  })

  useEffect(() => {
    if (!all) return
    const alive = new Set(all.map(d => d.path))
    setPicked(p => {
      const next = new Set([...p].filter(x => alive.has(x)))
      return next.size === p.size ? p : next
    })
  }, [all])

  useEffect(() => {
    if (!menu) return
    const close = () => setMenu(null)
    const key = (e: KeyboardEvent) => e.key === 'Escape' && close()
    window.addEventListener('mousedown', close)
    window.addEventListener('blur', close)
    window.addEventListener('keydown', key)
    return () => {
      window.removeEventListener('mousedown', close)
      window.removeEventListener('blur', close)
      window.removeEventListener('keydown', key)
    }
  }, [menu])

  if (!demos) return <div className="list" />

  const toggle = (path: string) => {
    const next = new Set(picked)
    if (next.has(path)) next.delete(path)
    else next.add(path)
    setPicked(next)
    setAnchor(path)
  }

  const range = (path: string) => {
    const a = demos.findIndex(d => d.path === (anchor ?? selected))
    const b = demos.findIndex(d => d.path === path)
    if (a < 0 || b < 0) return toggle(path)
    const next = new Set(picked)
    for (let i = Math.min(a, b); i <= Math.max(a, b); i++) next.add(demos[i].path)
    setPicked(next)
  }

  const onClick = (e: React.MouseEvent, path: string) => {
    if (e.ctrlKey || e.metaKey) toggle(path)
    else if (e.shiftKey) range(path)
    else onSelect(path)
  }

  const onContext = (e: React.MouseEvent, path: string) => {
    e.preventDefault()
    const paths = picked.has(path) && picked.size > 1 ? [...picked] : [path]
    setMenu({ x: Math.min(e.clientX, window.innerWidth - 220), y: Math.min(e.clientY, window.innerHeight - 240), paths, path })
  }

  const act = (fn: () => void) => () => {
    setMenu(null)
    fn()
  }

  const remove = (paths: string[]) => {
    send({ type: 'delete_demos', paths })
    setConfirm(null)
    setPicked(new Set())
  }

  const many = picked.size > 0

  return (
    <>
      <div className={`list${many ? ' picking' : ''}`} role="list">
        {words.length > 0 && demos.length === 0 && <div className="list-empty">{t('noMatches')}</div>}
        {demos.map(d => {
          const r = rosters[d.path]
          const src = r ? source(r.server, t) : ''
          const names = r ? teamLabels(r, me, t) : []
          const on = picked.has(d.path)
          return (
            <div
              key={d.path}
              role="listitem"
              tabIndex={0}
              className={`item${d.path === selected ? ' on' : ''}${on ? ' picked' : ''}`}
              onClick={e => onClick(e, d.path)}
              onKeyDown={e => e.key === 'Enter' && onSelect(d.path)}
              onContextMenu={e => onContext(e, d.path)}
            >
              {r && art[r.map] && <img className="item-art" src={`https://cs2voice-maps.example/${r.map}.png`} alt="" />}
              <span
                className="item-check"
                role="checkbox"
                aria-checked={on}
                aria-label={t('select')}
                onClick={e => {
                  e.stopPropagation()
                  toggle(d.path)
                }}
              />
              <span className="item-map">{r ? mapName(r.map, t) : d.name.replace(/\.dem(\.zst)?$/i, '')}</span>
              {r && r.players.length > 0 && (
                <span className="item-teams">
                  <span className="tn">{names[0]}</span>
                  <span className={`item-score${r.partial && !r.ended ? ' wait' : ''}`}>
                    <span className="t">{r.partial && !r.ended ? '–' : r.teams[0].score}</span>
                    <span className="sep">:</span>
                    <span className="ct">{r.partial && !r.ended ? '–' : r.teams[1].score}</span>
                  </span>
                  <span className="tn right">{names[1]}</span>
                </span>
              )}
              <span className="item-meta">{when(d.mtime, t, lang)}</span>
              <span className="item-tags">
                {src && <span className="tag">{src}</span>}
                {d.keep && <span className="tag keep">{t('keep')}</span>}
                {!r ? (
                  <span className="tag wait">{t('reading')}</span>
                ) : r.voice > 0 ? (
                  <span className="tag voice">{t('voice')}</span>
                ) : (
                  (!r.partial || r.voice_known) && <span className="tag mute">{t('noVoice')}</span>
                )}
              </span>
            </div>
          )
        })}
      </div>

      {many && (
        <div className="pick-bar">
          <span>{t('pickedCount', { n: picked.size })}</span>
          <button onClick={() => send({ type: 'move_demos', paths: [...picked] })}>{t('moveTo')}</button>
          <button className="danger" onClick={() => setConfirm([...picked])}>
            {t('delete')}
          </button>
          <button className="pick-clear" onClick={() => setPicked(new Set())} aria-label={t('clearPick')}>
            <svg viewBox="0 0 10 10" width="10" height="10" aria-hidden="true">
              <path d="M1 1l8 8M9 1l-8 8" stroke="currentColor" strokeWidth="1.4" />
            </svg>
          </button>
        </div>
      )}

      {menu && (
        <div className="menu" style={{ left: menu.x, top: menu.y }} onMouseDown={e => e.stopPropagation()} role="menu">
          {menu.paths.length === 1 && menu.path !== selected && (
            <button role="menuitem" onClick={act(() => onSelect(menu.path))}>
              {t('open')}
            </button>
          )}
          <button role="menuitem" onClick={act(() => toggle(menu.path))}>
            {picked.has(menu.path) ? t('deselect') : t('select')}
          </button>
          <button role="menuitem" onClick={act(() => send({ type: 'reveal', path: menu.path }))}>
            {t('showInFolder')}
          </button>
          <button role="menuitem" onClick={act(() => send({ type: 'copy_paths', paths: menu.paths }))}>
            {menu.paths.length > 1 ? t('copyPaths', { n: menu.paths.length }) : t('copyPath')}
          </button>
          <button
            role="menuitem"
            onClick={act(() => {
              const on = !menu.paths.every(p => demos.find(d => d.path === p)?.keep)
              send({ type: 'keep', paths: menu.paths, on })
            })}
          >
            {menu.paths.every(p => demos.find(d => d.path === p)?.keep) ? t('unkeep') : t('keep')}
          </button>
          <hr />
          <button role="menuitem" onClick={act(() => send({ type: 'move_demos', paths: menu.paths }))}>
            {menu.paths.length > 1 ? t('moveToMany', { n: menu.paths.length }) : t('moveTo')}
          </button>
          <button role="menuitem" className="danger" onClick={act(() => setConfirm(menu.paths))}>
            {menu.paths.length > 1 ? t('deleteMany', { n: menu.paths.length }) : t('delete')}
          </button>
        </div>
      )}

      {confirm && (
        <div className="modal" onMouseDown={() => setConfirm(null)}>
          <div className="dialog" onMouseDown={e => e.stopPropagation()} role="alertdialog">
            <p>
              {confirm.length > 1
                ? t('confirmDeleteMany', { n: confirm.length })
                : t('confirmDelete', { name: demos.find(d => d.path === confirm[0])?.name ?? '' })}
            </p>
            <div className="dialog-buttons">
              <button onClick={() => setConfirm(null)}>{t('cancel')}</button>
              <button className="danger" onClick={() => remove(confirm)} autoFocus>
                {t('delete')}
              </button>
            </div>
          </div>
        </div>
      )}
    </>
  )
}
