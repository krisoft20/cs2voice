import { useEffect, useMemo, useRef, useState, type MouseEvent } from 'react'
import { send, type Clip, type ClipFile, type ReplayMatch } from './bridge'
import { remember, remembered, useUi } from './common'
import type { Lang, T } from './i18n'
import { CheckIcon, ChevronLeftIcon, ChevronRightIcon, CloseIcon, CopyIcon, FolderIcon, PlayIcon, SearchIcon, StarIcon, TrashIcon, VideoIcon } from './icons'
import { mapName, when } from './voice'

const HOST = 'https://cs2voice-clips.example/'
const TAG = /^(ACE( clutch 1v\d)?|[2-4]K|clutch 1v\d|Noscope|Wallbang)\b/i
const HOVER_MS = 220
const TOAST_MS = 2200

type Sort = 'new' | 'old' | 'big' | 'long'
type Filter = 'all' | 'fav' | 'ace' | 'clutch' | '4k' | '3k' | '2k' | `map:${string}`

const urlOf = (f: ClipFile) => HOST + encodeURIComponent(f.file)
const thumbOf = (f: ClipFile) => `${HOST}thumb/${encodeURIComponent(f.file.replace(/\.mp4$/, '.jpg'))}?v=${f.mtime}`
const titleOf = (f: ClipFile) => f.file.replace(/\.mp4$/, '')
const tagOf = (f: ClipFile) => TAG.exec(titleOf(f))?.[0] ?? ''
const lenOf = (f: ClipFile) => f.meta?.len || (f.meta ? f.meta.to - f.meta.from : 0)

const megabytes = (bytes: number) =>
  bytes >= 1e9 ? `${(bytes / 1e9).toFixed(1)} GB` : `${bytes >= 10e6 ? Math.round(bytes / 1e6) : (bytes / 1e6).toFixed(1)} MB`

const length = (secs: number) => {
  const s = Math.max(0, Math.round(secs))
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}

const dayOf = (unix: number) => {
  const d = new Date(unix * 1000)
  return new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
}

const dayLabel = (day: number, t: T, lang: Lang) => {
  const today = dayOf(Date.now() / 1000)
  const diff = Math.round((today - day) / 86400000)
  if (diff === 0) return t('clipsToday')
  if (diff === 1) return t('clipsYesterday')
  return new Intl.DateTimeFormat(lang, { weekday: 'long', day: 'numeric', month: 'long' }).format(day)
}

const mapOf = (f: ClipFile, t: T) => {
  const m = f.meta?.map
  if (!m) return ''
  return m.startsWith('de_') || m.startsWith('cs_') ? mapName(m, t) : m
}

const matches = (f: ClipFile, filter: Filter, t: T) => {
  const tag = tagOf(f).toLowerCase()
  if (filter === 'all') return true
  if (filter === 'fav') return !!f.meta?.fav
  if (filter === 'ace') return tag.startsWith('ace')
  if (filter === 'clutch') return tag.includes('clutch')
  if (filter.startsWith('map:')) return mapOf(f, t) === filter.slice(4)
  return tag === filter
}

const tagText = (tag: string) => (/^(ace|[2-4]k)/i.test(tag) ? tag.slice(0, 3).toUpperCase() + tag.slice(3) : tag)

function Title(props: { f: ClipFile; editing: boolean; onEdit: (on: boolean) => void }) {
  const { f, editing, onEdit } = props
  const title = titleOf(f)
  const [text, setText] = useState(title)
  useEffect(() => setText(title), [title, editing])
  if (!editing)
    return (
      <strong
        onDoubleClick={e => {
          e.stopPropagation()
          onEdit(true)
        }}
      >
        {title}
      </strong>
    )
  const done = (save: boolean) => {
    onEdit(false)
    if (save && text.trim() && text.trim() !== title) send({ type: 'clip_rename', path: f.path, name: text.trim() })
  }
  return (
    <input
      className="ck-rename"
      autoFocus
      value={text}
      spellCheck={false}
      onFocus={e => e.currentTarget.select()}
      onClick={e => e.stopPropagation()}
      onChange={e => setText(e.target.value)}
      onBlur={() => done(true)}
      onKeyDown={e => {
        e.stopPropagation()
        if (e.key === 'Enter') done(true)
        else if (e.key === 'Escape') done(false)
      }}
    />
  )
}

function ClipCard(props: {
  f: ClipFile
  picked: boolean
  picking: boolean
  canGo: boolean
  onOpen: () => void
  onPick: (range: boolean) => void
  onCopy: () => void
  onGo: () => void
}) {
  const { f, picked, picking, canGo, onOpen, onPick, onCopy, onGo } = props
  const { t, lang } = useUi()
  const [live, setLive] = useState(false)
  const [shown, setShown] = useState(false)
  const [armed, setArmed] = useState(false)
  const [editing, setEditing] = useState(false)
  const timer = useRef(0)
  const tag = tagOf(f)
  const len = lenOf(f)
  const map = mapOf(f, t)
  const fav = !!f.meta?.fav
  useEffect(() => {
    if (!armed) return
    const id = window.setTimeout(() => setArmed(false), 3000)
    return () => clearTimeout(id)
  }, [armed])
  useEffect(() => () => clearTimeout(timer.current), [])
  const hover = (on: boolean) => {
    clearTimeout(timer.current)
    if (on) timer.current = window.setTimeout(() => setLive(true), HOVER_MS)
    else {
      setLive(false)
      setShown(false)
    }
  }
  const click = (e: MouseEvent) => {
    if (editing) return
    if (e.ctrlKey || e.metaKey || e.shiftKey || picking) onPick(e.shiftKey)
    else onOpen()
  }
  return (
    <div
      className={`ck-card${picked ? ' picked' : ''}${picking ? ' picking' : ''}`}
      role="button"
      tabIndex={0}
      onClick={click}
      onKeyDown={e => e.key === 'Enter' && !editing && onOpen()}
      onMouseEnter={() => hover(true)}
      onMouseLeave={() => hover(false)}
    >
      {f.thumb ? <img src={thumbOf(f)} alt="" decoding="async" draggable={false} /> : <span className="ck-blank" />}
      {live && <video className={shown ? 'on' : ''} src={urlOf(f)} autoPlay muted loop playsInline onPlaying={() => setShown(true)} />}
      <span className="ck-shade" />
      <span className="ck-top">
        <button
          className={`ck-pick${picked ? ' on' : ''}`}
          onClick={e => {
            e.stopPropagation()
            onPick(e.shiftKey)
          }}
          aria-label={t('clipSelect')}
        >
          <CheckIcon />
        </button>
        {tag && <span className="ck-tag">{tagText(tag)}</span>}
        <span className="ck-gap" />
        <button
          className={`ck-star${fav ? ' on' : ''}`}
          onClick={e => {
            e.stopPropagation()
            send({ type: 'clip_fav', path: f.path, on: !fav })
          }}
          aria-label={fav ? t('clipUnfav') : t('clipFav')}
        >
          <StarIcon />
        </button>
        {len > 0 && <span className="ck-dur">{length(len)}</span>}
      </span>
      <span className="ck-info">
        <Title f={f} editing={editing} onEdit={setEditing} />
        <small>
          {[map, when(f.mtime, t, lang), megabytes(f.bytes)].filter(Boolean).join(', ')}
        </small>
      </span>
      <span className="ck-tools" onClick={e => e.stopPropagation()}>
        {canGo && (
          <button onClick={onGo} aria-label={t('clipGoto')}>
            <VideoIcon />
          </button>
        )}
        <button onClick={onCopy} aria-label={t('clipCopy')}>
          <CopyIcon />
        </button>
        <button onClick={() => send({ type: 'clip_show', path: f.path })} aria-label={t('clipShow')}>
          <FolderIcon />
        </button>
        <button
          className={armed ? 'danger' : ''}
          onClick={() => (armed ? send({ type: 'clip_delete', paths: [f.path] }) : setArmed(true))}
          aria-label={armed ? t('deleteConfirm') : t('delete')}
        >
          <TrashIcon />
          {armed && <span>{t('deleteConfirm')}</span>}
        </button>
      </span>
    </div>
  )
}

function Viewer(props: { f: ClipFile; list: ClipFile[]; canGo: boolean; onMove: (f: ClipFile) => void; onClose: () => void; onCopy: () => void; onGo: () => void }) {
  const { f, list, canGo, onMove, onClose, onCopy, onGo } = props
  const { t, lang } = useUi()
  const [editing, setEditing] = useState(false)
  const [armed, setArmed] = useState(false)
  const i = list.findIndex(x => x.path === f.path)
  const prev = i > 0 ? list[i - 1] : null
  const next = i >= 0 && i < list.length - 1 ? list[i + 1] : null
  const fav = !!f.meta?.fav
  useEffect(() => setArmed(false), [f.path])
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (editing) return
      if (e.key === 'Escape') onClose()
      else if (e.key === 'ArrowLeft' && prev) onMove(prev)
      else if (e.key === 'ArrowRight' && next) onMove(next)
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [editing, prev, next, onClose, onMove])
  const map = mapOf(f, t)
  return (
    <div className="ck-view" onClick={e => e.target === e.currentTarget && onClose()}>
      {prev && (
        <button className="ck-nav prev" onClick={() => onMove(prev)} aria-label={t('clipPrev')}>
          <ChevronLeftIcon />
        </button>
      )}
      <div className="ck-view-box">
        <video key={f.path} src={urlOf(f)} poster={f.thumb ? thumbOf(f) : undefined} controls autoPlay />
        <div className="ck-view-bar">
          <span className="ck-view-title">
            <Title f={f} editing={editing} onEdit={setEditing} />
            <small>{[map, when(f.mtime, t, lang), megabytes(f.bytes)].filter(Boolean).join(', ')}</small>
          </span>
          <button className={`ck-icon ck-star${fav ? ' on' : ''}`} onClick={() => send({ type: 'clip_fav', path: f.path, on: !fav })} aria-label={fav ? t('clipUnfav') : t('clipFav')}>
            <StarIcon />
          </button>
          {canGo && (
            <button className="ghost" onClick={onGo}>
              <VideoIcon />
              {t('clipGoto')}
            </button>
          )}
          <button className="ghost" onClick={onCopy}>
            <CopyIcon />
            {t('clipCopyShort')}
          </button>
          <button className="ck-icon" onClick={() => setEditing(true)} aria-label={t('clipRename')}>
            <span className="ck-aa">Aa</span>
          </button>
          <button className="ck-icon" onClick={() => send({ type: 'clip_show', path: f.path })} aria-label={t('clipShow')}>
            <FolderIcon />
          </button>
          <button
            className={`ck-icon${armed ? ' danger wide' : ''}`}
            onClick={() => (armed ? send({ type: 'clip_delete', paths: [f.path] }) : setArmed(true))}
            aria-label={armed ? t('deleteConfirm') : t('delete')}
          >
            <TrashIcon />
            {armed && <span>{t('deleteConfirm')}</span>}
          </button>
          <button className="ck-icon" onClick={onClose} aria-label={t('close')}>
            <CloseIcon />
          </button>
        </div>
      </div>
      {next && (
        <button className="ck-nav next" onClick={() => onMove(next)} aria-label={t('clipNext')}>
          <ChevronRightIcon />
        </button>
      )}
    </div>
  )
}

export function ClipsPage(props: {
  files: ClipFile[] | null
  bytes: number
  making: Clip[]
  matches: ReplayMatch[] | null
  onGoTo: (match: string, t: number) => void
}) {
  const { files, bytes, making, matches: replays, onGoTo } = props
  const { t, lang } = useUi()
  const [open, setOpen] = useState<ClipFile | null>(null)
  const [query, setQuery] = useState('')
  const [filter, setFilter] = useState<Filter>('all')
  const [sort, setSort] = useState<Sort>(() => (remembered('clipsSort', 'new') as Sort) || 'new')
  const [picked, setPicked] = useState<Set<string>>(new Set())
  const [anchor, setAnchor] = useState<string | null>(null)
  const [armed, setArmed] = useState(false)
  const [toast, setToast] = useState('')
  const busy = making.filter(c => c.state === 'making')
  const all = files ?? []

  const playable = useMemo(() => new Set((replays ?? []).filter(m => m.video).map(m => m.id)), [replays])
  const canGo = (f: ClipFile) => !!f.meta?.match && playable.has(f.meta.match)
  const goTo = (f: ClipFile) => f.meta && onGoTo(f.meta.match, f.meta.from)

  const chips = useMemo(() => {
    const out: { id: Filter; label: string; n: number }[] = [{ id: 'all', label: t('clipsAll'), n: all.length }]
    const add = (id: Filter, label: string) => {
      const n = all.filter(f => matches(f, id, t)).length
      if (n) out.push({ id, label, n })
    }
    add('fav', t('clipsFavs'))
    add('ace', 'ACE')
    add('clutch', t('clipsClutches'))
    add('4k', '4K')
    add('3k', '3K')
    add('2k', '2K')
    const maps = [...new Set(all.map(f => mapOf(f, t)).filter(Boolean))].sort()
    if (maps.length > 1) for (const m of maps) add(`map:${m}`, m)
    return out
  }, [all, t])

  const shown = useMemo(() => {
    const q = query.trim().toLowerCase()
    const list = all.filter(f => matches(f, filter, t) && (!q || titleOf(f).toLowerCase().includes(q) || mapOf(f, t).toLowerCase().includes(q)))
    if (sort === 'old') list.reverse()
    else if (sort === 'big') list.sort((a, b) => b.bytes - a.bytes)
    else if (sort === 'long') list.sort((a, b) => lenOf(b) - lenOf(a))
    return list
  }, [all, filter, query, sort, t])

  useEffect(() => {
    if (!chips.some(c => c.id === filter)) setFilter('all')
  }, [chips, filter])

  const before = useRef<ClipFile[]>([])
  useEffect(() => {
    if (!files) return
    const old = before.current
    before.current = files
    if (open) {
      const renamed = files.find(f => !old.some(o => o.path === f.path) && f.mtime === open.mtime && f.bytes === open.bytes)
      const now = files.find(f => f.path === open.path) ?? renamed
      if (now !== open) setOpen(now ?? null)
    }
    setPicked(p => {
      const keep = new Set([...p].filter(x => files.some(f => f.path === x)))
      return keep.size === p.size ? p : keep
    })
  }, [files, open])

  useEffect(() => {
    if (!armed) return
    const id = window.setTimeout(() => setArmed(false), 3000)
    return () => clearTimeout(id)
  }, [armed])

  useEffect(() => {
    if (!toast) return
    const id = window.setTimeout(() => setToast(''), TOAST_MS)
    return () => clearTimeout(id)
  }, [toast])

  useEffect(() => {
    if (open) return
    const key = (e: KeyboardEvent) => {
      const typing = e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement
      if (typing) return
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'a') {
        e.preventDefault()
        setPicked(new Set(shown.map(f => f.path)))
      } else if (e.key === 'Escape') setPicked(new Set())
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [open, shown])

  const copy = (paths: string[]) => {
    send({ type: 'clip_copy', paths })
    setToast(paths.length > 1 ? t('clipsCopiedMany', { n: paths.length }) : t('clipsCopied'))
  }

  const pick = (f: ClipFile, range: boolean) => {
    setPicked(p => {
      const next = new Set(p)
      const a = range && anchor ? shown.findIndex(x => x.path === anchor) : -1
      const b = shown.findIndex(x => x.path === f.path)
      if (a >= 0 && b >= 0) for (let i = Math.min(a, b); i <= Math.max(a, b); i++) next.add(shown[i].path)
      else if (next.has(f.path)) next.delete(f.path)
      else next.add(f.path)
      return next
    })
    setAnchor(f.path)
  }

  const groups: { key: string; label: string; items: ClipFile[] }[] = []
  if (sort === 'new' || sort === 'old')
    for (const f of shown) {
      const day = dayOf(f.mtime)
      const last = groups[groups.length - 1]
      if (last && last.key === String(day)) last.items.push(f)
      else groups.push({ key: String(day), label: dayLabel(day, t, lang), items: [f] })
    }
  else if (shown.length) groups.push({ key: 'all', label: '', items: shown })

  const card = (f: ClipFile) => (
    <ClipCard
      key={f.path}
      f={f}
      picked={picked.has(f.path)}
      picking={picked.size > 0}
      canGo={canGo(f)}
      onOpen={() => setOpen(f)}
      onPick={range => pick(f, range)}
      onCopy={() => copy([f.path])}
      onGo={() => goTo(f)}
    />
  )

  return (
    <main className="main ck-main">
      <header className="ck-head">
        <span className="ck-title">
          <h1>{t('clips')}</h1>
          {all.length > 0 && <span>{t('clipsTotal', { n: all.length, size: megabytes(bytes) })}</span>}
        </span>
        {all.length > 0 && (
          <span className="ck-controls">
            <label className="ck-search">
              <SearchIcon />
              <input value={query} onChange={e => setQuery(e.target.value)} placeholder={t('clipsSearch')} spellCheck={false} />
              {query && (
                <button onClick={() => setQuery('')} aria-label={t('close')}>
                  <CloseIcon />
                </button>
              )}
            </label>
            <select
              className="ck-sort"
              value={sort}
              onChange={e => {
                setSort(e.target.value as Sort)
                remember('clipsSort', e.target.value)
              }}
            >
              <option value="new">{t('clipsNewest')}</option>
              <option value="old">{t('clipsOldest')}</option>
              <option value="big">{t('clipsLargest')}</option>
              <option value="long">{t('clipsLongest')}</option>
            </select>
          </span>
        )}
        <button className="ghost ck-folder" onClick={() => send({ type: 'clip_show', path: '' })}>
          <FolderIcon />
          {t('clipsFolder')}
        </button>
      </header>
      {chips.length > 1 && (
        <nav className="ck-chips">
          {chips.map(c => (
            <button key={c.id} className={filter === c.id ? 'on' : ''} onClick={() => setFilter(c.id)}>
              {c.id === 'fav' && <StarIcon />}
              {c.label}
              <small>{c.n}</small>
            </button>
          ))}
        </nav>
      )}
      {files && !all.length && !busy.length && (
        <div className="ck-empty">
          <span className="ck-empty-art">
            <PlayIcon />
          </span>
          <p>{t('clipsEmpty')}</p>
        </div>
      )}
      {all.length > 0 && !shown.length && <p className="ck-none">{t('clipsNoMatch')}</p>}
      {busy.length > 0 && (
        <div className="ck-grid ck-busy">
          {busy.map(c => (
            <div key={c.id} className="ck-card making">
              <span className="ck-info">
                <strong>{c.name}</strong>
                <small>{t('clipMaking', { n: Math.round(c.p * 100) })}</small>
              </span>
              <span className="ck-progress">
                <i style={{ width: `${Math.round(c.p * 100)}%` }} />
              </span>
            </div>
          ))}
        </div>
      )}
      {groups.map(g => (
        <section key={g.key} className="ck-day">
          {g.label && <h2>{g.label}</h2>}
          <div className="ck-grid">{g.items.map(card)}</div>
        </section>
      ))}
      {picked.size > 0 && (
        <div className="ck-bar">
          <strong>{t('clipsSelected', { n: picked.size })}</strong>
          <button className="ghost" onClick={() => setPicked(new Set(shown.map(f => f.path)))}>
            {t('clipsSelectAll')}
          </button>
          <button className="ghost" onClick={() => copy([...picked])}>
            <CopyIcon />
            {t('clipCopyShort')}
          </button>
          <button
            className={`ghost${armed ? ' danger' : ''}`}
            onClick={() => {
              if (!armed) return setArmed(true)
              send({ type: 'clip_delete', paths: [...picked] })
              setArmed(false)
            }}
          >
            <TrashIcon />
            {armed ? t('deleteConfirm') : t('clipsDeleteMany', { n: picked.size })}
          </button>
          <button className="ck-icon" onClick={() => setPicked(new Set())} aria-label={t('clearPick')}>
            <CloseIcon />
          </button>
        </div>
      )}
      {toast && <div className="ck-toast">{toast}</div>}
      {open && (
        <Viewer
          f={open}
          list={shown}
          canGo={canGo(open)}
          onMove={setOpen}
          onClose={() => setOpen(null)}
          onCopy={() => copy([open.path])}
          onGo={() => goTo(open)}
        />
      )}
    </main>
  )
}
