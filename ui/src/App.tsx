import { useCallback, useEffect, useId, useLayoutEffect, useMemo, useRef, useState } from 'react'
import type { ReactNode } from 'react'
import { listen, send, type CleanupCfg, type SetupInfo, type Demo, type FaceitPlayer, type Folder, type Player, type Profile, type Roster, type Stats, type Storage, type PerfMsg, type PerfRow, type SkinsMsg, type SkinItem } from './bridge'
import { LANGS, gameLang, resolve, translator, type Key, type Lang, type LangChoice } from './i18n'
import { BoltIcon, BombIcon, CalendarIcon, CheckIcon, ChevronDownIcon, ChevronLeftIcon, ChevronRightIcon, ClockIcon, CloseIcon, CloudIcon, CrownIcon, DemosIcon, DiskIcon, DoorIcon, FlameIcon, FolderIcon, FolderPlusIcon, HelpIcon, InfoIcon, LanguageIcon, LockIcon, LogoMark, MicIcon, MicOffIcon, MinusIcon, PaletteIcon, PlayIcon, PlusIcon, RefreshIcon, SearchIcon, SettingsIcon, ShieldIcon, Speaker, SteamIcon, SunIcon, TargetIcon, TradeIcon, UserMinusIcon, UsersIcon, WallIcon } from './icons'
import { Avatar, UiContext, useUi, type Loader } from './common'
import { decode, draw } from './crosshair'
import { Check, CleanupCard, CleanupReview, CleanupSettings } from './Cleanup'
import { planCleanup } from './cleanplan'
import { DemoList } from './DemoList'
import { Details } from './Details'
import { MEASURES, bins, median, rating, type Measure } from './stats'
import { REFERENCE } from './reference'
import { Radar, type RadarInfo } from './Radar'
import { usePlayback } from './playback'
import { parseTrack, timelineOf } from './trackdata'
import { parse } from './voicedata'
import { Voices } from './Voices'
import { command, length, mapName, masks, sideName, source, teamLabels, when, rounds } from './voice'


type Page = 'demos' | 'settings'
type Place = { page: Page; selected: string | null }

const SPIN_MS = 800

export default function App() {
  const [demos, setDemos] = useState<Demo[] | null>(null)
  const [folders, setFolders] = useState<Folder[]>([])
  const [rosters, setRosters] = useState<Record<string, Roster>>({})
  const [reading, setReading] = useState<Record<string, boolean>>({})
  const [selected, setSelected] = useState<string | null>(null)
  const [chosen, setChosen] = useState<Set<number>>(new Set())
  const [copied, setCopied] = useState('')
  const [avatars, setAvatars] = useState<Record<string, string>>({})
  const [profiles, setProfiles] = useState<Record<string, Profile>>({})
  const [me, setMe] = useState('')
  const [art, setArt] = useState<Record<string, boolean>>({})
  const [choice, setChoice] = useState<LangChoice>('system')
  const [version, setVersion] = useState('')
  const [updates, setUpdates] = useState(true)
  const [steam, setSteam] = useState(true)
  const [update, setUpdate] = useState<{ version: string; notes: string; size: number; store: boolean } | null>(null)
  const [updating, setUpdating] = useState<{ state: 'idle' | 'downloading' | 'failed'; done: number; total: number; err: string }>({
    state: 'idle',
    done: 0,
    total: 0,
    err: '',
  })
  const [showUpdate, setShowUpdate] = useState(false)
  const [closeMode, setCloseMode] = useState<CloseMode>('ask')
  const [asking, setAsking] = useState(false)
  const [setupInfo, setSetupInfo] = useState<SetupInfo | null>(null)
  const [setupErr, setSetupErr] = useState('')
  const [loader, setLoader] = useState<Loader>('radar')
  const [spinning, setSpinning] = useState(false)
  const [voice, setVoice] = useState<{ path: string; data?: ArrayBuffer; err: string } | null>(null)
  const [track, setTrack] = useState<{ path: string; data?: ArrayBuffer; err: string } | null>(null)
  const [radars, setRadars] = useState<Record<string, RadarInfo>>({})
  const [perfs, setPerfs] = useState<Record<string, PerfMsg>>({})
  const [skins, setSkins] = useState<Record<string, SkinsMsg>>({})
  const [econ, setEcon] = useState<Set<string>>(new Set())
  const [icons, setIcons] = useState(false)
  const [saved, setSaved] = useState('')
  const [watch, setWatch] = useState<{ state: string; err?: string } | null>(null)
  const [fixes, setFixes] = useState<Record<string, Fix>>({})
  const [faceit, setFaceit] = useState<Record<string, FaceitPlayer[]>>({})
  const [search, setSearch] = useState<string | null>(null)
  const [storage, setStorage] = useState<Storage[]>([])
  const [cleanup, setCleanup] = useState<CleanupCfg>({ mode: 'ask', method: 'recycle', days: 7, copies: true, broken: true, mute: false, snooze: 0, skip: [] })
  const [review, setReview] = useState(false)
  const [settingsCat, setSettingsCat] = useState<SettingsCat>('general')
  const autoDone = useRef(false)
  const [maximized, setMaximized] = useState(false)
  const [help, setHelp] = useState(false)
  const spinStart = useRef(0)
  const artAsked = useRef(new Set<string>())
  const [page, setPage] = useState<Page>(location.hash === '#settings' ? 'settings' : 'demos')

  const [past, setPast] = useState<Place[]>([])
  const [future, setFuture] = useState<Place[]>([])
  const here = useRef<Place>({ page, selected })
  const travelling = useRef(false)

  useEffect(() => {
    const prev = here.current
    const now = { page, selected }
    here.current = now
    if (travelling.current) {
      travelling.current = false
      return
    }
    if (prev.page === now.page && prev.selected === now.selected) return
    if (prev.selected === null && now.page === prev.page) return
    setPast(p => [...p.slice(-49), prev])
    setFuture([])
  }, [page, selected])

  const go = (to: Place) => {
    travelling.current = true
    setPage(to.page)
    setSelected(to.selected)
  }
  const back = () => {
    const to = past[past.length - 1]
    if (!to) return
    setPast(p => p.slice(0, -1))
    setFuture(f => [here.current, ...f])
    go(to)
  }
  const forward = () => {
    const to = future[0]
    if (!to) return
    setFuture(f => f.slice(1))
    setPast(p => [...p, here.current])
    go(to)
  }
  const navRef = useRef({ back, forward })
  navRef.current = { back, forward }

  useEffect(() => {
    const mouse = (e: MouseEvent) => {
      if (e.button === 3) navRef.current.back()
      else if (e.button === 4) navRef.current.forward()
      else return
      e.preventDefault()
    }
    window.addEventListener('mouseup', mouse)
    return () => window.removeEventListener('mouseup', mouse)
  }, [])

  const lang = resolve(choice)
  const ui = useMemo(() => ({ t: translator(lang), lang, me, loader }), [lang, me, loader])

  useEffect(() => {
    listen(m => {
      if (m.type === 'demos') {
        if (spinStart.current) {
          const left = SPIN_MS - ((performance.now() - spinStart.current) % SPIN_MS)
          spinStart.current = 0
          setTimeout(() => setSpinning(false), left)
        }
        setDemos(m.items)
        setRosters(r => {
          const next = { ...r }
          for (const d of m.items) if (d.info) next[d.path] = d.info
          return next
        })
        const want = new URLSearchParams(location.hash.slice(1)).get('demo')
        setSelected(s => s ?? (want && m.items.find(d => d.path.includes(want))?.path) ?? m.items[0]?.path ?? null)
      } else if (m.type === 'folders') setFolders(m.items)
      else if (m.type === 'roster') {
        setRosters(r => ({ ...r, [m.path]: m }))
        setReading(r => ({ ...r, [m.path]: false }))
      } else if (m.type === 'reading') setReading(r => ({ ...r, [m.path]: true }))
      else if (m.type === 'copied') setCopied(m.text)
      else if (m.type === 'profiles') {
        const urls: Record<string, string> = {}
        for (const [id, p] of Object.entries(m.items)) {
          if (!p.avatar) continue
          urls[id] = `https://cs2voice-avatars.example/${id}.jpg?v=${p.avatar}`
          new Image().src = urls[id]
        }
        setAvatars(a => ({ ...a, ...urls }))
        setProfiles(f => ({ ...f, ...m.items }))
      }
      else if (m.type === 'me') setMe(m.steamid)
      else if (m.type === 'art') setArt(a => ({ ...a, [m.map]: true }))
      else if (m.type === 'voice') setVoice({ path: m.path, data: m.data, err: m.err })
      else if (m.type === 'track') setTrack({ path: m.path, data: m.data, err: m.err })
      else if (m.type === 'radar') setRadars(r => ({ ...r, [m.map]: m }))
      else if (m.type === 'perf') setPerfs(p => ({ ...p, [m.path]: m }))
      else if (m.type === 'skins') setSkins(p => ({ ...p, [m.path]: m }))
      else if (m.type === 'econ_img') setEcon(p => new Set(p).add(m.img))
      else if (m.type === 'icons') setIcons(true)
      else if (m.type === 'saved') setSaved(m.path)
      else if (m.type === 'storage') setStorage(m.items)
      else if (m.type === 'window') setMaximized(m.maximized)
      else if (m.type === 'cleared') send({ type: 'storage' })
      else if (m.type === 'cache_cleared') {
        setRosters({})
        setReading({})
        setAvatars({})
        setProfiles({})
        setArt({})
        setRadars({})
        artAsked.current.clear()
        setSelected(s => {
          if (s) send({ type: 'open', path: s })
          return s
        })
      }
      else if (m.type === 'watch') {
        setWatch({ state: m.state, err: m.err })
        if (m.state === 'copied') setTimeout(() => setWatch(w => (w?.state === m.state ? null : w)), 2500)
      }
      else if (m.type === 'faceit') setFaceit(f => ({ ...f, [m.path]: m.players }))
      else if (m.type === 'fixed') {
        setFixes(f => ({ ...f, [m.path]: m.ok ? { state: 'done' } : { state: 'error', err: m.err } }))
      }
      else if (m.type === 'close_ask') setAsking(true)
      else if (m.type === 'setup_error') setSetupErr(m.err)
      else if (m.type === 'update') setUpdate({ version: m.version, notes: m.notes, size: m.size ?? 0, store: !!m.store })
      else if (m.type === 'update_state') setUpdating({ state: m.state, done: m.done ?? 0, total: m.total ?? 0, err: m.err ?? '' })
      else if (m.type === 'settings') {
        setChoice(LANGS.some(l => l.id === m.lang) ? (m.lang as Lang) : 'system')
        setVersion(m.version)
        setUpdates(m.updates)
        if (m.steam !== undefined) setSteam(m.steam)
        if (m.close === 'ask' || m.close === 'tray' || m.close === 'quit') setCloseMode(m.close)
        if (m.setup) setSetupInfo(m.setup)
        if (m.loader === 'plant' || m.loader === 'radar' || m.loader === 'rounds' || m.loader === 'ammo') setLoader(m.loader)
        if (m.cleanup) setCleanup(m.cleanup)
      }
    })
    send({ type: 'ready' })
  }, [])

  useEffect(() => {
    document.documentElement.lang = lang
    send({ type: 'tray_text', open: translator(lang)('trayOpen'), quit: translator(lang)('trayQuit') })
  }, [lang])

  useEffect(() => {
    if (!selected) return
    setChosen(new Set())
    setCopied('')
    send({ type: 'open', path: selected })
  }, [selected])

  const demo = demos?.find(d => d.path === selected) ?? null
  const known = useMemo(() => {
    const out: Record<string, Roster> = {}
    for (const d of demos ?? []) {
      const r = rosters[d.path] ?? d.info
      if (r) out[d.path] = r
    }
    return out
  }, [demos, rosters])
  const plan = useMemo(() => (demos && cleanup.mode !== 'off' ? planCleanup(demos, known, cleanup, selected) : []), [demos, known, cleanup, selected])
  const closeReview = useCallback(() => setReview(false), [])

  useEffect(() => {
    if (cleanup.mode !== 'auto' || !demos || autoDone.current) return
    if (demos.some(d => !known[d.path])) return
    autoDone.current = true
    const paths = plan.flatMap(r => r.files.map(f => f.path))
    if (paths.length) send({ type: 'cleanup', paths })
  }, [cleanup, demos, known, plan])
  useEffect(() => {
    if (cleanup.mode !== 'auto') autoDone.current = false
  }, [cleanup.mode])
  const rawRoster = selected ? rosters[selected] : undefined
  const roster = useMemo(() => withFaceit(rawRoster, selected ? faceit[selected] : undefined), [rawRoster, selected, faceit])
  useEffect(() => {
    if (selected && rawRoster?.ok && !faceit[selected]) send({ type: 'faceit', path: selected })
  }, [selected, !!rawRoster?.ok])
  const pool = useMemo(() => {
    const out: Stats[] = []
    for (const r of Object.values(rosters))
      if (!r.partial) for (const p of r.players) if (p.stats && p.stats.rounds > 0) out.push(p.stats)
    return out
  }, [rosters])

  useEffect(() => {
    if (roster?.players.length) send({ type: 'profiles', ids: roster.players.map(p => p.steamid) })
    if (roster?.map) send({ type: 'art', map: roster.map })
  }, [roster])

  useEffect(() => {
    for (const r of Object.values(rosters)) {
      if (!r.map || artAsked.current.has(r.map)) continue
      artAsked.current.add(r.map)
      send({ type: 'art', map: r.map })
    }
  }, [rosters])

  const pickLang = (c: LangChoice) => {
    setChoice(c)
    send({ type: 'set', key: 'lang', value: c })
  }

  return (
    <UiContext.Provider value={ui}>
      <div className="shell">
      <TitleBar
        canBack={past.length > 0}
        canForward={future.length > 0}
        onBack={back}
        onForward={forward}
        onHelp={() => setHelp(true)}
        update={update?.version ?? ''}
        onUpdate={() => setShowUpdate(true)}
        title={page === 'settings' ? ui.t('settings') : roster ? `${mapName(roster.map, ui.t)}${(!roster.partial || roster.ended) && roster.rounds > 0 ? `  ${roster.teams[0].score} : ${roster.teams[1].score}` : ''}` : ''}
        maximized={maximized}
      />
      <div className={`app page-${page}`}>
        <Rail
          page={page}
          onPage={p => {
            setSettingsCat('general')
            setPage(p)
          }}
        />
        {page === 'demos' ? (
          <>
            <aside className="side">
              <header className="side-head">
                <span>cs2voice</span>
                <span className="side-tools">
                <button
                  className={`icon-btn${search !== null ? ' on' : ''}`}
                  onClick={() => setSearch(s => (s === null ? '' : null))}
                  aria-label={ui.t('search')}
                >
                  <SearchIcon />
                </button>
                <button
                  className={`icon-btn${spinning ? ' spinning' : ''}`}
                  onClick={() => {
                    if (!spinStart.current) spinStart.current = performance.now()
                    setSpinning(true)
                    send({ type: 'rescan' })
                  }}
                  aria-label={ui.t('scan')}
                >
                  <span className="spin" style={{ animationDuration: `${SPIN_MS}ms` }}>
                    <RefreshIcon />
                  </span>
                </button>
                </span>
              </header>
              {search !== null && (
                <div className="side-search">
                  <input
                    autoFocus
                    value={search}
                    placeholder={ui.t('searchHint')}
                    onChange={e => setSearch(e.target.value)}
                    onKeyDown={e => e.key === 'Escape' && setSearch(null)}
                  />
                </div>
              )}
              {cleanup.mode === 'ask' && plan.length > 0 && Date.now() / 1000 > cleanup.snooze && (
                <CleanupCard
                  plan={plan}
                  cfg={cleanup}
                  onReview={() => setReview(true)}
                  onSettings={() => {
                    setSettingsCat('demos')
                    setPage('settings')
                  }}
                />
              )}
              <DemoList demos={demos} rosters={rosters} art={art} selected={selected} onSelect={setSelected} query={search ?? ''} />
            </aside>
            <main className="main">
              {demo ? (
                <DemoView
                  key={demo.path}
                  demo={demo}
                  roster={roster}
                  reading={!!reading[demo.path] || !roster}
                  chosen={chosen}
                  setChosen={setChosen}
                  copied={copied}
                  avatars={avatars}
                  profiles={profiles}
                  art={!!roster && !!art[roster.map]}
                  pool={pool}
                  voice={voice?.path === demo.path ? voice : null}
                  track={track?.path === demo.path ? track : null}
                  radar={roster ? radars[roster.map] ?? null : null}
                  perf={perfs[demo.path] ?? null}
                  skins={skins[demo.path] ?? null}
                  econ={econ}
                  icons={icons}
                  saved={saved}
                  watch={watch}
                  onWatch={() => setWatch({ state: 'asked' })}
                  fix={fixes[demo.path]}
                  onFix={() => {
                    setFixes(f => ({ ...f, [demo.path]: { state: 'running' } }))
                    send({ type: 'fix_demo', path: demo.path })
                  }}
                />
              ) : (
                <Empty loading={demos === null} />
              )}
            </main>
          </>
        ) : (
          <Settings
            cat={settingsCat}
            version={version}
            steam={steam}
            onSteam={on => {
              setSteam(on)
              send({ type: 'set', key: 'steam', value: on ? '1' : '0' })
            }}
            updates={updates}
            onUpdates={on => {
              setUpdates(on)
              send({ type: 'set', key: 'updates', value: on ? '1' : '0' })
            }}
            cleanup={cleanup}
            plan={plan}
            onReview={() => setReview(true)}
            choice={choice}
            onLang={pickLang}
            folders={folders}
            storage={storage}
            loader={loader}
            onLoader={l => {
              setLoader(l)
              send({ type: 'set', key: 'loader', value: l })
            }}
            setup={setupInfo}
            setupErr={setupErr}
            onInstall={(desktop, autostart) => {
              setSetupErr('')
              send({ type: 'install', desktop, autostart })
            }}
            closeMode={closeMode}
            onCloseMode={c => {
              setCloseMode(c)
              send({ type: 'set', key: 'close', value: c })
            }}
          />
        )}
      </div>
      </div>
      {help && <Help version={version} onClose={() => setHelp(false)} />}
      {asking && (
        <CloseAsk
          onPick={(c, remember) => {
            setAsking(false)
            if (remember) {
              setCloseMode(c)
              send({ type: 'set', key: 'close', value: c })
            }
            send({ type: 'win', action: c })
          }}
          onCancel={() => {
            setAsking(false)
            send({ type: 'win', action: 'stay' })
          }}
        />
      )}
      {showUpdate && update && <Update version={update.version} current={version} notes={update.notes} size={update.size} store={update.store} state={updating} onClose={() => setShowUpdate(false)} />}
      {review && plan.length > 0 && <CleanupReview plan={plan} cfg={cleanup} onClose={closeReview} />}
    </UiContext.Provider>
  )
}

const REPO = 'https://github.com/krisoft20/cs2voice'

const CREDITS = [
  ['Microsoft Edge WebView2', 'BSD-3-Clause'],
  ['React', 'MIT'],
  ['Vite', 'MIT'],
  ['Barlow fonts by Jeremy Tribby', 'OFL-1.1'],
  ['Tabler Icons', 'MIT'],
  ['Simple Icons (Steam)', 'CC0-1.0'],
]

function Update(props: {
  version: string
  current: string
  notes: string
  size: number
  store: boolean
  state: { state: 'idle' | 'downloading' | 'failed'; done: number; total: number; err: string }
  onClose: () => void
}) {
  const { t, lang } = useUi()
  const { version, current, notes, size, store, state, onClose } = props
  const busy = state.state === 'downloading'
  const lines = notes
    .split('\n')
    .map(l => l.trim().replace(/^([-*]|#+)\s*/, ''))
    .filter(Boolean)
  const mb = (n: number) => `${(n / (1024 * 1024)).toLocaleString(lang, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} MB`
  const filled = state.total ? Math.round((40 * state.done) / state.total) : 0
  useEffect(() => {
    const key = (e: KeyboardEvent) => e.key === 'Escape' && !busy && onClose()
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  })
  return (
    <div className="modal" onMouseDown={busy ? undefined : onClose}>
      <div className="whatsnew" onMouseDown={e => e.stopPropagation()} role="dialog" aria-label={t('whatsNew')}>
        <div className="wn-title">
          <span>{t('whatsNew')}</span>
          <button onClick={onClose} disabled={busy} aria-label={t('close')}>
            x
          </button>
        </div>
        <div className="wn-body">
          <h2>{version ? t('updateTitle', { v: version.replace(/^v/, '') }) : t('updateTitleStore')}</h2>
          <div className="wn-notes">
            <ul>
              {lines.map((l, i) => (
                <li key={i}>{l}</li>
              ))}
            </ul>
          </div>
          {state.state === 'failed' ? (
            <p className="wn-err">{t('updateFailed', { err: state.err })}</p>
          ) : busy ? (
            <p>{t('updateProgress', { done: mb(state.done), total: mb(state.total) })}</p>
          ) : (
            <p>{store ? t('updateHaveStore', { v: current }) : t('updateHave', { v: current, size: mb(size) })}</p>
          )}
          {busy && (
            <div className="wn-bar">
              {Array.from({ length: filled }, (_, i) => (
                <i key={i} />
              ))}
            </div>
          )}
          <div className="wn-buttons">
            <button onClick={onClose} disabled={busy}>
              {t('later')}
            </button>
            <button className="go" onClick={() => send({ type: 'update_install' })} disabled={busy} autoFocus>
              {t('updateInstall')}
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

type CloseMode = 'ask' | 'tray' | 'quit'

function CloseAsk(props: { onPick: (c: 'tray' | 'quit', remember: boolean) => void; onCancel: () => void }) {
  const { t } = useUi()
  const [remember, setRemember] = useState(true)
  useEffect(() => {
    const key = (e: KeyboardEvent) => e.key === 'Escape' && props.onCancel()
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  })
  return (
    <div className="modal" onMouseDown={props.onCancel}>
      <div className="dialog close-dialog" onMouseDown={e => e.stopPropagation()} role="dialog" aria-label={t('closeTitle')}>
        <h2>{t('closeTitle')}</h2>
        <p>{t('closeText')}</p>
        <Check on={remember} onChange={setRemember} label={t('closeRemember')} />
        <div className="dialog-buttons">
          <button onClick={() => props.onPick('quit', remember)}>{t('closeQuit')}</button>
          <button className="go" onClick={() => props.onPick('tray', remember)} autoFocus>
            {t('closeTray')}
          </button>
        </div>
      </div>
    </div>
  )
}

function About(props: { version: string }) {
  const { t } = useUi()
  const link = (url: string, text: string) => (
    <button className="link" onClick={() => send({ type: 'open_url', url })}>
      {text}
    </button>
  )
  return (
    <div className="about">
      <div className="help-head">
        <LogoMark />
        <div>
          <h2>cs2voice</h2>
          <span>{t('version', { v: props.version })}</span>
        </div>
      </div>
      <p>{t('helpWhat')}</p>
      <p>{t('helpPrivacy')}</p>
      <p className="free">{t('helpFree')}</p>
      <dl className="help-facts">
        <dt>{t('helpAuthor')}</dt>
        <dd>{link('https://github.com/krisoft20', 'krisoft20')}</dd>
        <dt>{t('helpSource')}</dt>
        <dd>{link(REPO, 'github.com/krisoft20/cs2voice')}</dd>
        <dt>{t('helpLicense')}</dt>
        <dd>{link(`${REPO}/blob/main/LICENSE`, 'GPL-3.0')}</dd>
        <dt>{t('helpUses')}</dt>
        <dd>
          {CREDITS.map(([name, lic]) => (
            <span key={name} className="credit">
              {name} <small>{lic}</small>
            </span>
          ))}
        </dd>
      </dl>
    </div>
  )
}

function Help(props: { version: string; onClose: () => void }) {
  const { t } = useUi()
  useEffect(() => {
    const key = (e: KeyboardEvent) => e.key === 'Escape' && props.onClose()
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  })
  return (
    <div className="modal" onMouseDown={props.onClose}>
      <div className="dialog help-dialog" onMouseDown={e => e.stopPropagation()} role="dialog" aria-label={t('help')}>
        <About version={props.version} />
        <div className="dialog-buttons">
          <button onClick={props.onClose} autoFocus>
            {t('close')}
          </button>
        </div>
      </div>
    </div>
  )
}

function TitleBar(props: {
  title: string
  update: string
  onUpdate: () => void
  maximized: boolean
  canBack: boolean
  canForward: boolean
  onBack: () => void
  onForward: () => void
  onHelp: () => void
}) {
  const { t } = useUi()
  return (
    <header className="titlebar">
      <div className="nav-buttons">
        <button onClick={props.onBack} disabled={!props.canBack} aria-label={t('back')}>
          <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
            <path d="M13 8H3M7.5 3.5L3 8l4.5 4.5" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
        </button>
        <button onClick={props.onForward} disabled={!props.canForward} aria-label={t('forward')}>
          <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
            <path d="M3 8h10M8.5 3.5L13 8l-4.5 4.5" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
        </button>
      </div>
      <span className="titlebar-title">{props.title}</span>
      <div className="window-buttons">
        {props.update && (
          <button className="update" onClick={props.onUpdate} aria-label={t('updateAvailable', { v: props.update })}>
            <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
              <path d="M8 1.5v8.2M4.2 6.2L8 10l3.8-3.8" fill="none" stroke="currentColor" strokeWidth="1.9" strokeLinecap="round" strokeLinejoin="round" />
              <path d="M2.8 14h10.4" stroke="currentColor" strokeWidth="1.9" strokeLinecap="round" />
            </svg>
          </button>
        )}
        <button className="help" onClick={props.onHelp} aria-label={t('help')}>
          <HelpIcon />
        </button>
        <button onClick={() => send({ type: 'win', action: 'minimize' })} aria-label={t('minimize')}>
          <svg viewBox="0 0 10 10" width="10" height="10" aria-hidden="true">
            <path d="M0 5h10" stroke="currentColor" strokeWidth="1" />
          </svg>
        </button>
        <button onClick={() => send({ type: 'win', action: 'maximize' })} aria-label={props.maximized ? t('restore') : t('maximize')}>
          {props.maximized ? (
            <svg viewBox="0 0 10 10" width="10" height="10" aria-hidden="true">
              <path d="M2.5 2.5V0.5h7v7h-2M0.5 2.5h7v7h-7z" fill="none" stroke="currentColor" strokeWidth="1" />
            </svg>
          ) : (
            <svg viewBox="0 0 10 10" width="10" height="10" aria-hidden="true">
              <path d="M0.5 0.5h9v9h-9z" fill="none" stroke="currentColor" strokeWidth="1" />
            </svg>
          )}
        </button>
        <button className="close" onClick={() => send({ type: 'win', action: 'close' })} aria-label={t('close')}>
          <svg viewBox="0 0 10 10" width="10" height="10" aria-hidden="true">
            <path d="M0.5 0.5l9 9M9.5 0.5l-9 9" stroke="currentColor" strokeWidth="1" />
          </svg>
        </button>
      </div>
    </header>
  )
}

function Rail(props: { page: Page; onPage: (p: Page) => void }) {
  const { t } = useUi()
  const { page, onPage } = props
  return (
    <nav className="rail" aria-label="cs2voice">
      <span className="logo" aria-hidden="true">
        <LogoMark />
      </span>
      <button className={`rail-btn${page === 'demos' ? ' on' : ''}`} onClick={() => onPage('demos')} aria-label={t('demos')}>
        <DemosIcon />
      </button>
      <button
        className={`rail-btn bottom${page === 'settings' ? ' on' : ''}`}
        onClick={() => onPage('settings')}
        aria-label={t('settings')}
      >
        <SettingsIcon />
      </button>
    </nav>
  )
}

const withFaceit = (r: Roster | undefined, fp: FaceitPlayer[] | undefined): Roster | undefined => {
  if (!r || !fp?.length) return r
  const by = new Map(fp.map(f => [f.steamid, f]))
  return {
    ...r,
    players: r.players.map(p => {
      const f = by.get(p.steamid)
      return f ? { ...p, name: f.nick, demoName: f.nick !== p.name ? p.name : undefined, level: f.level, elo: f.elo, rank: f.rank } : p
    }),
  }
}

type Fix = { state: 'running' | 'done' | 'error'; err?: string }

function DemoView(props: {
  demo: Demo
  roster?: Roster
  reading: boolean
  chosen: Set<number>
  setChosen: (s: Set<number>) => void
  copied: string
  avatars: Record<string, string>
  profiles: Record<string, Profile>
  art: boolean
  pool: Stats[]
  voice: { data?: ArrayBuffer; err: string } | null
  track: { data?: ArrayBuffer; err: string } | null
  radar: RadarInfo | null
  perf: PerfMsg | null
  skins: SkinsMsg | null
  econ: Set<string>
  icons: boolean
  saved: string
  watch: { state: string; err?: string } | null
  onWatch: () => void
  fix?: Fix
  onFix: () => void
}) {
  const { t, lang, me } = useUi()
  const { demo, roster, chosen, setChosen, copied, avatars, profiles, art, pool, voice, track, radar, perf, skins, econ, icons, saved, watch, onWatch, fix, onFix } = props
  const [seenVoices, setSeenVoices] = useState(false)
  const [tab, setTab] = useState<'overview' | 'players' | 'details' | 'voices' | 'radar' | 'crosshairs' | 'skins'>(
    () => (new URLSearchParams(location.hash.slice(1)).get('tab') as 'players' | null) ?? 'overview',
  )
  const [focus, setFocus] = useState<number | null>(null)
  const tabsRef = useRef<HTMLElement>(null)
  const [bar, setBar] = useState<{ x: number; w: number } | null>(null)
  const busy = watch?.state === 'asked' || watch?.state === 'unpacking'
  useEffect(() => {
    if (tab === 'details' && !icons) send({ type: 'icons' })
  }, [tab, icons])
  useEffect(() => {
    if (tab === 'skins' && roster) send({ type: 'skins', path: demo.path, lang: gameLang(lang) })
  }, [tab, demo.path, !!roster, roster?.partial, roster?.stale, lang])
  const copyDemo = () => {
    if (!roster) return
    onWatch()
    send({ type: 'watch', path: demo.path, fix: fix?.state === 'done', ...masks(chosen) })
  }
  const teams = useMemo(() => {
    const out: Player[][] = [[], []]
    for (const p of roster?.players ?? []) if (p.team === 0 || p.team === 1) out[p.team].push(p)
    for (const team of out) team.sort((a, b) => (b.stats?.damage ?? 0) - (a.stats?.damage ?? 0))
    return out
  }, [roster])
  const labels = roster ? teamLabels(roster, me, t) : ['', '']
  const loudest = Math.max(1, ...(roster?.players.map(p => p.voice) ?? [1]))
  const cmd = command(chosen)
  const partial = !!roster?.partial
  const scored = !!roster && (!partial || !!roster.ended) && roster.rounds > 0
  const hasVoice = !partial && (roster?.voice ?? 0) > 0
  const hasFaceit = !!roster?.players.some(p => (p.level ?? 0) > 0)
  const hasStats = !partial && !!roster?.players.some(p => p.stats && p.stats.rounds > 0)
  const hasDetails = hasStats && !roster?.stale && !!roster?.winners
  const voiceData = voice?.data
  const trackData = track?.data
  const voiceTimeline = useMemo(() => (voiceData ? parse(voiceData) : null), [voiceData])
  const trackParsed = useMemo(() => (trackData ? parseTrack(trackData) : null), [trackData])
  const timeline = useMemo(
    () => (hasVoice ? voiceTimeline : trackParsed ? timelineOf(trackParsed) : null),
    [hasVoice, voiceTimeline, trackParsed],
  )
  const pb = usePlayback(timeline, tab === 'voices' || tab === 'radar')
  const best = useMemo(() => {
    const players = roster?.players ?? []
    const mine = players.find(p => p.steamid === me)
    if (mine) return mine.slot
    let top: Player | null = null
    for (const p of players) if (p.stats && (!top || (rating(p.stats) ?? -1) > (rating(top.stats!) ?? -1))) top = p
    return top?.slot ?? null
  }, [roster, me])
  const focused = roster?.players.find(p => p.slot === (focus ?? best))
  const mvp = useMemo(() => {
    if (!roster || roster.partial) return null
    let top: Player | null = null
    for (const p of roster.players)
      if (p.stats && p.stats.rounds > 0 && (!top || (rating(p.stats) ?? -1) > (rating(top.stats!) ?? -1))) top = p
    return top?.slot ?? null
  }, [roster])

  const toggle = (slot: number) => {
    const next = new Set(chosen)
    if (next.has(slot)) next.delete(slot)
    else next.add(slot)
    setChosen(next)
  }
  const only = (slots: number[]) => setChosen(new Set(slots))
  const isOnly = (slots: number[]) => slots.length > 0 && slots.length === chosen.size && slots.every(s => chosen.has(s))
  const hasTabs = !!roster && roster.players.length > 0
  const statsWait = !!roster && (partial || !!roster.stale)
  const perfWait = !!roster && !perf
  const voiceWait = !!roster && (partial || (hasVoice && !voice))
  const radarWait = !!roster && (partial || !track)

  useEffect(() => {
    if (!roster || partial || !hasTabs) return
    if (hasVoice && !voice) send({ type: 'voice', path: demo.path })
    if (!track) send({ type: 'track', path: demo.path })
    if (!radar) send({ type: 'radar', map: roster.map })
  }, [demo.path, partial, hasTabs])

  useLayoutEffect(() => {
    const nav = tabsRef.current
    if (!nav) return
    const place = () => {
      const on = nav.querySelector<HTMLElement>('button.on')
      if (on) setBar({ x: on.offsetLeft, w: on.offsetWidth })
    }
    place()
    const watch = new ResizeObserver(place)
    nav.querySelectorAll('button').forEach(b => watch.observe(b))
    return () => watch.disconnect()
  }, [tab, hasTabs, lang])
  const everyone = roster?.players.map(p => p.slot) ?? []

  return (
    <div className="view">
      <header className={`hero${art ? ' with-art' : ''}${tab === 'radar' ? ' slim' : ''}`}>
        {art && roster && <img className="hero-art" src={`https://cs2voice-maps.example/${roster.map}.png`} alt="" />}
        <div className="hero-map">{roster ? mapName(roster.map, t) : demo.name}</div>
        {roster && roster.players.length > 0 && (
          <div className="hero-side">
            <button
              className={`hear-all${isOnly(everyone) ? ' on' : ''}`}
              onClick={() => only(isOnly(everyone) ? [] : everyone)}
              aria-label={t('hearEveryone')}
              aria-pressed={isOnly(everyone)}
            >
              <UsersIcon />
            </button>
            <button className={`watch${watch?.state === 'copied' ? ' done' : ''}`} onClick={copyDemo}>
              {watch?.state === 'copied' ? <CheckIcon /> : <PlayIcon />}
              <span className="watch-text">
                {busy ? t('watchPreparing') : watch?.state === 'copied' ? t('watchCopied') : t('watch')}
                {!busy && watch?.state !== 'copied' && <small>{t('watchSub')}</small>}
              </span>
            </button>
            {watch?.state === 'error' && <span className="watch-note bad">{t('watchFailed', { err: watch.err ?? '' })}</span>}
          </div>
        )}
        {roster && (!partial || roster.ended) && roster.rounds > 0 && (
          <div className="hero-score">
            <span className="t">{roster.teams[0].score}</span>
            <span className="sep">:</span>
            <span className="ct">{roster.teams[1].score}</span>
          </div>
        )}
        <div className="hero-meta">
          {roster && <span className="meta-chip">{source(roster.server, t)}</span>}
          <span className="meta-item">
            <CalendarIcon />
            {when(demo.mtime, t, lang)}
          </span>
          {roster && roster.duration > 0 && (
            <span className="meta-item">
              <ClockIcon />
              {scored ? `${rounds(roster.rounds, t)}, ${length(roster.duration, t)}` : length(roster.duration, t)}
            </span>
          )}
          {scored && roster.rounds > 24 && <span className="meta-ot">{t('overtime')}</span>}
          {roster && !partial && !hasVoice && roster.players.length > 0 && <span className="tag mute">{t('noVoice')}</span>}
          <button className="meta-file" onClick={() => send({ type: 'reveal', path: demo.path })}>
            <FolderIcon />
            {t('showFile')}
          </button>
          {fix?.state === 'done' && <span className="tag voice">{t('fixedTag')}</span>}
        </div>
        {roster && roster.players.length > 0 && (
          <nav className="tabs" ref={tabsRef}>
            {bar && <span className="tab-bar" style={{ transform: `translateX(${bar.x}px)`, width: bar.w }} />}
            <button className={tab === 'overview' ? 'on' : ''} onClick={() => setTab('overview')}>
              {t('tabOverview')}
            </button>
            <button className={tab === 'players' ? 'on' : ''} onClick={() => setTab('players')} disabled={!hasStats}>
              <TabSpin on={statsWait || perfWait} />
              {t('tabPlayers')}
            </button>
            <button className={tab === 'details' ? 'on' : ''} onClick={() => setTab('details')} disabled={!hasDetails}>
              <TabSpin on={statsWait} />
              {t('tabDetails')}
            </button>
            <button
              className={tab === 'voices' ? 'on' : ''}
              onClick={() => {
                setTab('voices')
                setSeenVoices(true)
                if (!voice && hasVoice) send({ type: 'voice', path: demo.path })
              }}
              disabled={partial}
            >
              <TabSpin on={voiceWait} />
              {t('tabVoices')}
            </button>
            <button
              className={tab === 'radar' ? 'on' : ''}
              onClick={() => {
                setTab('radar')
                if (!track) send({ type: 'track', path: demo.path })
                if (!voice && hasVoice) send({ type: 'voice', path: demo.path })
                if (!radar && roster) send({ type: 'radar', map: roster.map })
              }}
              disabled={partial}
            >
              <TabSpin on={radarWait} />
              {t('tabRadar')}
            </button>
            <button className={tab === 'crosshairs' ? 'on' : ''} onClick={() => setTab('crosshairs')}>
              {t('tabCrosshairs')}
            </button>
            <button className={tab === 'skins' ? 'on' : ''} onClick={() => setTab('skins')}>
              <TabSpin on={tab === 'skins' && !skins} />
              {t('tabSkins')}
            </button>
          </nav>
        )}
      </header>

      {roster && !roster.ok && roster.players.length === 0 && (
        <div className="notice bad">{t('unreadable', { err: roster.err || t('unknownFormat') })}</div>
      )}

      {roster?.pet_crash && fix?.state !== 'done' && (
        <div className="notice pet">
          <span>{fix?.state === 'error' ? t('petFixFailed', { err: fix.err ?? '' }) : t('petCrash')}</span>
          <button onClick={onFix} disabled={fix?.state === 'running'}>
            {fix?.state === 'running' ? t('petFixing') : t('petFix')}
          </button>
        </div>
      )}

      {roster && hasStats && tab === 'players' && (
        <div className="scroll pane">
          <div className="players">
            <nav className="player-list" aria-label={t('tabPlayers')}>
              {[0, 1].map(i => (
                <div key={i} className={`pl-group ${roster.teams[i]?.side === 3 ? 'ct' : 't'}`}>
                  <div className="pl-team">{labels[i]}</div>
                  {[...teams[i]].sort((x, y) => ((y.stats && rating(y.stats)) ?? -1) - ((x.stats && rating(x.stats)) ?? -1)).map(p => {
                    const r = p.stats ? rating(p.stats) : null
                    return (
                      <button key={p.slot} className={`pl-row${focused?.slot === p.slot ? ' on' : ''}`} onClick={() => setFocus(p.slot)}>
                        <Avatar url={avatars[p.steamid]} name={p.name} />
                        <span className="pl-who">
                          <span className="pl-name">{p.name || t('slot', { n: p.slot })}</span>
                          <SetupLine ping={p.stats?.ping} row={perf?.players.find(row => row.steamid === p.steamid)} />
                        </span>
                        {p.stats && (
                          <span className="pl-kda">
                            {p.stats.kills}/{p.stats.deaths}/{p.stats.assists}
                          </span>
                        )}
                        <span className="pl-rt" style={ratingTone(r)}>{fmt(r, 2, lang)}</span>
                      </button>
                    )
                  })}
                </div>
              ))}
            </nav>
            {focused?.stats && (
              <section className={`stats ${roster.teams[focused.team]?.side === 3 ? 'ct' : 't'}`}>
                <div className="stats-head">
                  <Avatar url={avatars[focused.steamid]} name={focused.name} />
                  <h2>{focused.name}</h2>
                  <span>{t('statsTitle')}</span>
                </div>
                <div className="tiles">
                  {MEASURES.map(m => (
                    <Tile key={m.key} measure={m} stats={focused.stats!} pool={pool} />
                  ))}
                  {PERF_TILES.map(k => {
                    const row = perf?.players.find(r => r.steamid === focused.steamid)
                    return <PerfTile key={k.key} kind={k} value={row ? row[k.key] : null} n={row ? row[`${k.key}_n`] : 0} waiting={!perf} />
                  })}
                  {SHOT_TILES.map(k => (
                    <ShotTile key={k.key} kind={k} row={perf?.players.find(r => r.steamid === focused.steamid)} waiting={!perf} />
                  ))}
                </div>
              </section>
            )}
          </div>
        </div>
      )}

      {roster && tab === 'skins' && (
        <div className="scroll pane xh-pane">
          <Skins roster={roster} teams={teams} labels={labels} avatars={avatars} msg={skins} econ={econ} />
        </div>
      )}

      {roster && tab === 'crosshairs' && (
        <div className="scroll pane xh-pane">
          <Crosshairs roster={roster} teams={teams} labels={labels} avatars={avatars} art={art} copied={copied} />
        </div>
      )}

      {roster && hasDetails && tab === 'details' && (
        <div className="scroll pane xh-pane">
          <Details roster={roster} avatars={avatars} icons={icons} />
        </div>
      )}

      {roster && roster.players.length > 0 && (tab === 'voices' || seenVoices) && !partial && (
        <div className="scroll xh-pane" hidden={tab !== 'voices'}>
          <Voices roster={roster} demo={demo.path} err={voice?.err} avatars={avatars} saved={saved} pb={pb} icons={icons} />
        </div>
      )}

      {roster && tab === 'radar' && !partial && (
        <div className="scroll xh-pane">
          <Radar roster={roster} track={trackParsed} err={track?.err} info={radar} pb={pb} icons={icons} avatars={avatars} />
        </div>
      )}

      {roster && roster.players.length > 0 && tab === 'overview' && (
        <div className="scroll pane">
        <div className="teams">
          <span className="vs">VS</span>
          {teams.map((members, i) => {
            const slots = members.map(p => p.slot)
            const side = roster.teams[i]?.side ?? 0
            return (
              <section key={i} className={`team ${side === 3 ? 'ct' : 't'}`}>
                <div className={`team-head${i === 0 ? ' left' : ''}`}>
                  <div>
                    <h2>{labels[i]}</h2>
                    <p>{t('startedAs', { side: sideName(side) })}</p>
                  </div>
                  <button
                    className={`solo${isOnly(slots) ? ' on' : ''}`}
                    onClick={() => only(isOnly(slots) ? [] : slots)}
                    disabled={!slots.length}
                    aria-label={t('hearTeam')}
                  >
                    <Speaker on={isOnly(slots)} />
                    {t('all')}
                  </button>
                </div>
                <div className="board-cols">
                  <span className="col-player">{t('colPlayerVoice')}</span>
                  {hasStats && (
                    <>
                      {hasFaceit && <span className="col-fc">FACEIT</span>}
                      <span className="col-kda">K / D / A</span>
                      <span className="col-rt">{t('colRating')}</span>
                    </>
                  )}
                </div>
                <ul className="channels">
                  {members.map(p => {
                    const on = chosen.has(p.slot)
                    const level = p.voice / loudest
                    return (
                      <li key={p.slot} className={`channel${on ? ' on' : ''}`}>
                        <button className="pick" onClick={() => toggle(p.slot)} aria-pressed={on}>
                          <Speaker on={on} />
                          <span
                            className="avatar-link"
                            role="link"
                            title={t('steamProfile')}
                            onClick={e => {
                              e.stopPropagation()
                              send({ type: 'steam', id: p.steamid })
                            }}
                          >
                            <Avatar url={avatars[p.steamid]} name={p.name} />
                            <SteamIcon />
                          </span>
                          <span className="who">
                            <span className="nickline">
                              <span className="nick" title={p.demoName ? t('demoNick', { name: p.demoName }) : undefined}>{p.name || t('slot', { n: p.slot })}</span>
                              {mvp === p.slot && <span className="mvp-chip">{t('awardMvp')}</span>}
                              <Badges profile={profiles[p.steamid]} mute={p.mute} />
                            </span>
                            {(hasVoice || (profiles[p.steamid]?.since ?? 0) > 0) && (
                              <span className="meterline">
                                {hasVoice ? (
                                  <span className="meter">
                                    <span style={{ width: `${Math.max(level * 100, p.voice ? 3 : 0)}%` }} />
                                  </span>
                                ) : (
                                  <span />
                                )}
                                {(profiles[p.steamid]?.since ?? 0) > 0 && (
                                  <span className="since" title={t('accountSince')}>
                                    {profiles[p.steamid]?.since}
                                  </span>
                                )}
                              </span>
                            )}
                          </span>
                          {hasVoice && !p.voice && <span className="silent">{t('silent')}</span>}
                        </button>
                        {hasStats && p.stats && (
                          <span className="line">
                            {hasFaceit && (
                              <span className="fc">
                                {(p.level ?? 0) > 0 && (
                                  <button className="fc-link" onClick={() => send({ type: 'faceit_profile', nick: p.name })}>
                                    <FaceitBadge level={p.level ?? 0} elo={p.elo ?? 0} rank={p.rank ?? 0} />
                                  </button>
                                )}
                              </span>
                            )}
                            <span className="kda">
                              {p.stats.kills}
                              <i>/</i>
                              {p.stats.deaths}
                              <i>/</i>
                              {p.stats.assists}
                            </span>
                            <span className="rt" style={ratingTone(rating(p.stats))}>{fmt(rating(p.stats), 2, lang)}</span>
                          </span>
                        )}
                      </li>
                    )
                  })}
                </ul>
              </section>
            )
          })}
        </div>
        {hasStats && <Awards roster={roster} avatars={avatars} mvp={mvp} hasVoice={hasVoice} />}
        </div>
      )}

      {roster && chosen.size > 0 && (
        <div className="voicepill" role="region" aria-label={t('voiceCommand')}>
          <span className="pill-faces">
            {roster.players
              .filter(p => chosen.has(p.slot))
              .slice(0, 5)
              .map(p => (
                <Avatar key={p.slot} url={avatars[p.steamid]} name={p.name} />
              ))}
          </span>
          <span className="pill-count">{isOnly(everyone) ? t('everyone') : chosen.size === 1 ? t('playerOne') : t('playersCount', { n: chosen.size })}</span>
          <code className="cmd">{cmd}</code>
          <button className={`copy${copied === cmd ? ' done' : ''}`} onClick={() => send({ type: 'copy', text: cmd })}>
            {copied === cmd ? t('copied') : t('copy')}
          </button>
          <button className="pill-clear" onClick={() => only([])} aria-label={t('nobody')}>
            <svg viewBox="0 0 10 10" width="10" height="10" aria-hidden="true">
              <path d="M1 1l8 8M9 1l-8 8" stroke="currentColor" strokeWidth="1.4" />
            </svg>
          </button>
        </div>
      )}
    </div>
  )
}

const STORAGE_LABELS: Record<string, Key> = {
  results: 'storageResults',
  unpacked: 'storageUnpacked',
  avatars: 'storageAvatars',
  maps: 'storageMaps',
  webview: 'storageWebview',
}

const size = (bytes: number, lang: string) => {
  const mb = bytes / (1024 * 1024)
  if (mb < 0.1) return `${Math.round(bytes / 1024).toLocaleString(lang)} KB`
  return `${mb.toLocaleString(lang, { maximumFractionDigits: 1 })} MB`
}

type SettingsCat = 'general' | 'look' | 'demos' | 'storage' | 'about'

function Settings(props: {
  cat: SettingsCat
  version: string
  updates: boolean
  onUpdates: (on: boolean) => void
  steam: boolean
  onSteam: (on: boolean) => void
  cleanup: CleanupCfg
  plan: ReturnType<typeof planCleanup>
  onReview: () => void
  choice: LangChoice
  onLang: (c: LangChoice) => void
  folders: Folder[]
  storage: Storage[]
  loader: Loader
  onLoader: (l: Loader) => void
  closeMode: CloseMode
  onCloseMode: (c: CloseMode) => void
  setup: SetupInfo | null
  setupErr: string
  onInstall: (desktop: boolean, autostart: boolean) => void
}) {
  const { t, lang } = useUi()
  const { version, updates, onUpdates, steam, onSteam, cleanup, plan, onReview, choice, onLang, folders, storage, loader, onLoader, closeMode, onCloseMode, setup, setupErr, onInstall } = props
  const closeModes: { id: CloseMode; name: Key }[] = [
    { id: 'ask', name: 'closeAskMode' },
    { id: 'tray', name: 'closeTray' },
    { id: 'quit', name: 'closeQuit' },
  ]
  const loaders: { id: Loader; name: Key }[] = [
    { id: 'radar', name: 'loaderRadar' },
    { id: 'plant', name: 'loaderPlant' },
    { id: 'rounds', name: 'loaderRounds' },
    { id: 'ammo', name: 'loaderAmmo' },
  ]
  const [cat, setCat] = useState<SettingsCat>(props.cat)
  const cats: { id: typeof cat; name: Key; icon: () => ReactNode }[] = [
    { id: 'general', name: 'catGeneral', icon: LanguageIcon },
    { id: 'look', name: 'catLook', icon: PaletteIcon },
    { id: 'demos', name: 'catDemos', icon: FolderIcon },
    { id: 'storage', name: 'storage', icon: DiskIcon },
    { id: 'about', name: 'about', icon: InfoIcon },
  ]
  useEffect(() => send({ type: 'storage' }), [])
  const options: { id: LangChoice; name: string }[] = [{ id: 'system', name: t('systemLanguage') }, ...LANGS.map(l => ({ id: l.id, name: l.name }))]
  return (
    <main className="settings">
      <h1>{t('settings')}</h1>

      <div className="settings-body">
        <nav className="settings-nav" aria-label={t('settings')}>
          {cats.map(c => (
            <button key={c.id} className={cat === c.id ? 'on' : ''} onClick={() => setCat(c.id)}>
              <c.icon />
              {t(c.name)}
            </button>
          ))}
        </nav>
        <div className="settings-pane" key={cat}>
          {cat === 'general' && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('language')}</h2>
              <p>{t('languageText')}</p>
            </div>
            <LangPicker options={options} choice={choice} onPick={onLang} />
          </section>
          )}
          {cat === 'general' && !setup?.store && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('updates')}</h2>
              <p>{t('updatesText')}</p>
            </div>
            <div className="choices" role="radiogroup" aria-label={t('updates')}>
              <button role="radio" aria-checked={updates} className={`choice${updates ? ' on' : ''}`} onClick={() => onUpdates(true)}>
                {t('updatesOn')}
              </button>
              <button role="radio" aria-checked={!updates} className={`choice${!updates ? ' on' : ''}`} onClick={() => onUpdates(false)}>
                {t('updatesOff')}
              </button>
            </div>
          </section>
          )}
          {cat === 'general' && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('steamSetting')}</h2>
              <p>{t('steamSettingText')}</p>
            </div>
            <div className="choices" role="radiogroup" aria-label={t('steamSetting')}>
              <button role="radio" aria-checked={steam} className={`choice${steam ? ' on' : ''}`} onClick={() => onSteam(true)}>
                {t('steamOn')}
              </button>
              <button role="radio" aria-checked={!steam} className={`choice${!steam ? ' on' : ''}`} onClick={() => onSteam(false)}>
                {t('steamOff')}
              </button>
            </div>
          </section>
          )}
          {cat === 'general' && setup && !setup.store && <InstallSetting setup={setup} err={setupErr} onInstall={onInstall} />}
          {cat === 'general' && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('closeSetting')}</h2>
              <p>{t('closeSettingText')}</p>
            </div>
            <div className="choices" role="radiogroup" aria-label={t('closeSetting')}>
              {closeModes.map(o => (
                <button key={o.id} role="radio" aria-checked={closeMode === o.id} className={`choice${closeMode === o.id ? ' on' : ''}`} onClick={() => onCloseMode(o.id)}>
                  {t(o.name)}
                </button>
              ))}
            </div>
          </section>
          )}
          {cat === 'look' && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('loaderTitle')}</h2>
              <p>{t('loaderText')}</p>
            </div>
            <div className="choices" role="radiogroup" aria-label={t('loaderTitle')}>
              {loaders.map(o => (
                <button key={o.id} role="radio" aria-checked={loader === o.id} className={`choice loader-choice${loader === o.id ? ' on' : ''}`} onClick={() => onLoader(o.id)}>
                  <LoadMark kind={o.id} />
                  {t(o.name)}
                </button>
              ))}
            </div>
          </section>
          )}
          {cat === 'demos' && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('folders')}</h2>
              <p>{t('foldersText')}</p>
            </div>
            <ul className="folder-list">
              {folders.map(f => (
                <li key={f.path}>
                  <span className="folder-path">
                    {f.path}
                  </span>
                  {f.custom ? (
                    <button className="icon-btn" onClick={() => send({ type: 'remove_folder', path: f.path })} aria-label={t('remove')}>
                      <CloseIcon />
                    </button>
                  ) : (
                    <span className="folder-note">{t('foundAutomatically')}</span>
                  )}
                </li>
              ))}
            </ul>
            <div className="row">
              <button className="ghost" onClick={() => send({ type: 'add_folder' })}>
                <FolderPlusIcon /> {t('addFolder')}
              </button>
              <button className="ghost" onClick={() => send({ type: 'rescan' })}>
                <RefreshIcon /> {t('scanAgain')}
              </button>
            </div>
          </section>
          )}
          {cat === 'demos' && <CleanupSettings cfg={cleanup} folders={folders} plan={plan} onReview={onReview} />}
          {cat === 'storage' && (
          <section className="setting">
            <div className="setting-head">
              <h2>{t('storage')}</h2>
              <p>{t('storageText')}</p>
            </div>
            <ul className="storage-list">
              {storage.map(s => (
                <li key={s.key}>
                  <span>{t(STORAGE_LABELS[s.key] ?? 'storageOther')}</span>
                  <span className="storage-size">{size(s.bytes, lang)}</span>
                  {s.key === 'unpacked' && (
                    <button className="ghost" onClick={() => send({ type: 'clear_unpacked' })} disabled={!s.files}>
                      {t('delete')}
                    </button>
                  )}
                </li>
              ))}
            </ul>
            <div className="row">
              <button className="ghost" onClick={() => send({ type: 'clear_cache' })}>
                {t('clearCache')}
              </button>
            </div>
          </section>
          )}
          {cat === 'about' && (
          <section className="setting about-setting">
            <About version={version} />
          </section>
          )}
        </div>
      </div>
    </main>
  )
}

const green = (k: number) => ({ color: `hsl(135 ${35 + 45 * k}% ${74 - 18 * k}%)` })
const red = (k: number) => ({ color: `hsl(6 ${45 + 45 * k}% ${74 - 16 * k}%)` })

const ratingTone = (r: number | null) => {
  if (r === null) return undefined
  const v = Math.round(r * 100) / 100
  if (v > 1) return green(Math.min(1, (v - 1) / 0.6))
  if (v < 0.6) return red(Math.min(1, (0.6 - v) / 0.4))
  return undefined
}

const NORMS: Partial<Record<Key, { ref: string; good: boolean; low?: boolean; open?: boolean }>> = {
  ttd: { ref: 'ttd', good: true, low: true },
  reaction: { ref: 'reaction', good: true, low: true },
  placement: { ref: 'placement', good: true, low: true },
  kd: { ref: 'kd', good: true },
  adr: { ref: 'adr', good: true },
  kast: { ref: 'kast', good: true },
  accuracy: { ref: 'accuracy', good: true },
  headAccuracy: { ref: 'head_hits', good: true },
  wallbangKills: { ref: 'wallbang', good: false },
  smokeKills: { ref: 'smoke', good: false },
  utilityRating: { ref: 'utility', good: true, open: true },
  firstShot: { ref: 'first_shot', good: true },
  counterStrafe: { ref: 'counter_strafe', good: true },
  swings: { ref: 'swing', good: true },
}

const normTone = (key: Key, unit: string, v: number | null) => {
  const norm = NORMS[key]
  const q = norm ? REFERENCE[norm.ref] : undefined
  if (!norm || !q || v === null) return undefined
  const x = unit === '%' ? v / 100 : v
  if (norm.low) {
    if (x < q[0]) return red(Math.min(1, (q[0] - x) / Math.max(q[1] - q[0], 1e-9)))
    if (x >= q[10]) return undefined
    let at = 10
    while (at > 1 && x < q[at - 1]) at--
    const part = Math.min(1, (q[at] - x) / Math.max(q[at] - q[at - 1], 1e-9))
    return green((10 - at + part) / 10)
  }
  const top = q[20]
  if (x > top && norm.open) return green(1)
  if (x > top) return red(Math.min(1, (x - top) / Math.max(top - q[19], 1e-9)))
  if (!norm.good || x <= q[10]) return undefined
  let at = 10
  while (at < 19 && x > q[at + 1]) at++
  const part = Math.min(1, (x - q[at]) / Math.max(q[at + 1] - q[at], 1e-9))
  return green((at + part - 10) / 10)
}

const fmt = (v: number | null, digits: number, lang: string) =>
  v === null ? '–' : v.toLocaleString(lang, { minimumFractionDigits: digits, maximumFractionDigits: digits })

function InstallSetting(props: { setup: SetupInfo; err: string; onInstall: (desktop: boolean, autostart: boolean) => void }) {
  const { t } = useUi()
  const { setup } = props
  const [desktop, setDesktop] = useState(true)
  const [autostart, setAutostart] = useState(false)
  const [confirm, setConfirm] = useState(false)
  const set = (key: string, on: boolean) => send({ type: 'set', key, value: on ? '1' : '0' })
  return (
    <section className="setting">
      <div className="setting-head">
        <h2>{t('installTitle')}</h2>
        <p>{setup.installed ? t('installedIn', { dir: setup.dir }) : t('installText')}</p>
      </div>
      <div className="install-box">
        {setup.installed ? (
          <>
            {!setup.here && <p className="install-note">{t('installElsewhere')}</p>}
            <Check on={setup.autostart} onChange={on => set('autostart', on)} label={t('installAutostart')} note={t('installAutostartNote')} />
            <Check on={setup.desktop} onChange={on => set('desktop', on)} label={t('installDesktop')} />
            <div className="row">
              <button className="ghost danger" onClick={() => setConfirm(true)}>
                {t('uninstall')}
              </button>
            </div>
          </>
        ) : (
          <>
            <Check on={desktop} onChange={setDesktop} label={t('installDesktop')} />
            <Check on={autostart} onChange={setAutostart} label={t('installAutostart')} note={t('installAutostartNote')} />
            <div className="row">
              <button className="install-go" onClick={() => props.onInstall(desktop, autostart)}>
                {t('installGo')}
              </button>
            </div>
          </>
        )}
        {props.err && <p className="install-err">{t('installFailed', { err: props.err })}</p>}
      </div>
      {confirm && <UninstallAsk onClose={() => setConfirm(false)} />}
    </section>
  )
}

function UninstallAsk(props: { onClose: () => void }) {
  const { t } = useUi()
  const [data, setData] = useState(false)
  useEffect(() => {
    const key = (e: KeyboardEvent) => e.key === 'Escape' && props.onClose()
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  })
  return (
    <div className="modal" onMouseDown={props.onClose}>
      <div className="dialog close-dialog" onMouseDown={e => e.stopPropagation()} role="dialog" aria-label={t('uninstallTitle')}>
        <h2>{t('uninstallTitle')}</h2>
        <p>{t('uninstallText')}</p>
        <Check on={data} onChange={setData} label={t('uninstallData')} />
        <div className="dialog-buttons">
          <button onClick={props.onClose} autoFocus>
            {t('cancel')}
          </button>
          <button className="danger" onClick={() => send({ type: 'uninstall', data })}>
            {t('uninstall')}
          </button>
        </div>
      </div>
    </div>
  )
}

function SetupLine(props: { ping?: number; row?: PerfRow }) {
  const parts: string[] = []
  if (props.ping) parts.push(`${props.ping} ms`)
  if (props.row?.fps) parts.push(`${Math.round(props.row.fps)} FPS`)
  if (props.row?.sens) parts.push(`${Number(props.row.sens.toFixed(3))} sens`)
  if (!parts.length) return null
  return (
    <span className="pl-setup">
      {parts.map(p => (
        <span key={p}>{p}</span>
      ))}
    </span>
  )
}

function LangPicker(props: { options: { id: LangChoice; name: string }[]; choice: LangChoice; onPick: (c: LangChoice) => void }) {
  const { t } = useUi()
  const [open, setOpen] = useState(false)
  const box = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!open) return
    const down = (e: MouseEvent) => !box.current?.contains(e.target as Node) && setOpen(false)
    const key = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false)
    window.addEventListener('mousedown', down)
    window.addEventListener('keydown', key)
    return () => {
      window.removeEventListener('mousedown', down)
      window.removeEventListener('keydown', key)
    }
  }, [open])
  const nameOf = (c: LangChoice) => (c === 'system' ? `${t('systemLanguage')} (${LANGS.find(l => l.id === resolve('system'))?.name})` : (LANGS.find(l => l.id === c)?.name ?? c))
  return (
    <div className="lang-pick" ref={box}>
      <button className={`lang-current${open ? ' open' : ''}`} onClick={() => setOpen(o => !o)} aria-haspopup="listbox" aria-expanded={open} aria-label={t('language')}>
        <span>{nameOf(props.choice)}</span>
        <ChevronDownIcon />
      </button>
      {open && (
        <ul className="lang-menu" role="listbox" aria-label={t('language')}>
          {props.options.map(o => (
            <li key={o.id}>
              <button
                role="option"
                aria-selected={props.choice === o.id}
                className={props.choice === o.id ? 'on' : ''}
                onClick={() => {
                  props.onPick(o.id)
                  setOpen(false)
                }}
              >
                <span>{nameOf(o.id)}</span>
                {props.choice === o.id && <CheckIcon />}
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  )
}

function LoadMark(props: { kind: Loader }) {
  switch (props.kind) {
    case 'radar':
      return <span className="l-mark l-radar" />
    case 'rounds':
      return (
        <span className="l-mark l-rounds">
          <i />
          <i />
          <i />
          <i />
          <i />
        </span>
      )
    case 'ammo':
      return (
        <span className="l-mark l-ammo">
          <i />
          <i />
          <i />
          <i />
          <i />
        </span>
      )
    default:
      return (
        <svg className="l-mark l-plant" viewBox="0 0 16 16" aria-hidden="true">
          <circle cx="8" cy="8" r="6.5" fill="none" stroke="rgba(255,255,255,.15)" strokeWidth="2" />
          <circle className="arc" cx="8" cy="8" r="6.5" fill="none" stroke="#ff8a3d" strokeWidth="2" strokeLinecap="round" transform="rotate(-90 8 8)" />
        </svg>
      )
  }
}

function TabSpin(props: { on: boolean }) {
  const { loader } = useUi()
  return (
    <span className={`tab-load k-${loader}${props.on ? '' : ' done'}`} aria-hidden="true">
      <LoadMark kind={loader} />
    </span>
  )
}

function Awards(props: { roster: Roster; avatars: Record<string, string>; mvp: number | null; hasVoice: boolean }) {
  const { t, lang } = useUi()
  const { roster, avatars, mvp, hasVoice } = props
  const players = roster.players.filter(p => p.stats && p.stats.rounds > 0)
  const score = (p: Player) => rating(p.stats!) ?? 0
  const adr = (s: Stats) => fmt(s.rounds ? s.damage / s.rounds : null, 0, lang)
  const pct = (n: number, d: number) => fmt(d ? (100 * n) / d : null, 0, lang)
  const won = (p: Player) => (roster.teams[p.team]?.score ?? 0) > (roster.teams[1 - p.team]?.score ?? 0)
  type Card = { key: string; p: Player; icon: () => ReactNode; label: string; text: string; sub: string[] }
  type Pick = { key: string; icon: () => ReactNode; label: string; of: (s: Stats) => number; text: (s: Stats) => string; sub: (s: Stats) => string[] }
  const POOL: Pick[] = [
    { key: 'entry', icon: DoorIcon, label: t('awardEntry'), of: s => s.entry_kills, text: s => t('awardEntryText', { n: s.entry_kills }), sub: s => [t('awardFirstDeaths', { n: s.entry_deaths })] },
    { key: 'clutch', icon: ShieldIcon, label: t('awardClutch'), of: s => s.clutches, text: s => t('awardClutchText', { n: s.clutches }), sub: s => [t('awardTried', { n: s.clutch_rounds })] },
    { key: 'damage', icon: FlameIcon, label: t('awardDamage'), of: s => s.damage, text: s => t('awardDamageText', { n: s.damage }), sub: s => [t('awardAdr', { n: adr(s) })] },
    { key: 'multi', icon: BoltIcon, label: t('awardMulti'), of: s => s.k3 + s.k4 + s.k5, text: s => t('awardMultiText', { n: s.k3 + s.k4 + s.k5 }), sub: s => (s.k4 + s.k5 ? [t('awardBig', { n: s.k4, m: s.k5 })] : []) },
    { key: 'trade', icon: TradeIcon, label: t('awardTrade'), of: s => s.trade_kills, text: s => t('awardTradeText', { n: s.trade_kills }), sub: s => [t('awardKillsShare', { n: pct(s.trade_kills, s.kills) })] },
    { key: 'utility', icon: BombIcon, label: t('awardUtility'), of: s => s.utility_damage, text: s => t('awardUtilityText', { n: s.utility_damage }), sub: s => [t('awardFlashed', { n: s.enemies_flashed })] },
    { key: 'flash', icon: SunIcon, label: t('awardFlash'), of: s => s.enemies_flashed, text: s => t('awardFlashed', { n: s.enemies_flashed }), sub: s => [t('awardFlashAssists', { n: s.flash_assists })] },
    { key: 'head', icon: TargetIcon, label: t('awardHead'), of: s => s.hs_kills, text: s => t('awardHeadText', { n: s.hs_kills }), sub: s => [t('awardHs', { n: pct(s.hs_kills, s.kills) })] },
    { key: 'wall', icon: WallIcon, label: t('awardWall'), of: s => s.wallbang_kills, text: s => t('awardWallText', { n: s.wallbang_kills }), sub: s => [t('awardKillsShare', { n: pct(s.wallbang_kills, s.kills) })] },
    { key: 'smoke', icon: CloudIcon, label: t('awardSmoke'), of: s => s.smoke_kills, text: s => t('awardSmokeText', { n: s.smoke_kills }), sub: s => [t('awardKillsShare', { n: pct(s.smoke_kills, s.kills) })] },
  ]
  const cards: Card[] = []
  const star = roster.players.find(p => p.slot === mvp)
  if (star?.stats) {
    const s = star.stats
    cards.push({
      key: 'mvp',
      p: star,
      icon: CrownIcon,
      label: t('awardMvp'),
      text: t('awardMvpText', { r: fmt(rating(s), 2, lang) }),
      sub: [`${s.kills}/${s.deaths}/${s.assists}`, t('awardAdr', { n: adr(s) }), t('awardHs', { n: pct(s.hs_kills, s.kills) })],
    })
  }
  const talk = roster.players.reduce((n, p) => n + p.voice, 0)
  const loudest = hasVoice && talk > 0 ? players.reduce<Player | null>((b, p) => (p.voice > (b?.voice ?? 0) ? p : b), null) : null
  const ranked = POOL.map(pick => {
    const values = players.map(p => pick.of(p.stats!))
    const mean = values.reduce((n, v) => n + v, 0) / Math.max(1, values.length)
    const spread = Math.sqrt(values.reduce((n, v) => n + (v - mean) ** 2, 0) / Math.max(1, values.length))
    let best: Player | null = null
    for (const p of players) {
      const v = pick.of(p.stats!)
      if (v > 0 && (!best || v > pick.of(best.stats!) || (v === pick.of(best.stats!) && score(p) > score(best)))) best = p
    }
    const z = best && spread > 0 ? (pick.of(best.stats!) - mean) / spread : -1
    return { pick, best, z }
  })
    .filter(r => r.best && r.z > 0)
    .sort((a, b) => Number(won(b.best!)) - Number(won(a.best!)) || b.z - a.z)
  const slots = loudest ? 2 : 3
  const taken = new Set(cards.map(c => c.p.slot))
  const chosen: typeof ranked = []
  for (const strict of [true, false])
    for (const r of ranked) {
      if (chosen.length >= slots) break
      if (chosen.includes(r) || (strict && taken.has(r.best!.slot))) continue
      chosen.push(r)
      taken.add(r.best!.slot)
    }
  for (const r of chosen)
    cards.push({ key: r.pick.key, p: r.best!, icon: r.pick.icon, label: r.pick.label, text: r.pick.text(r.best!.stats!), sub: r.pick.sub(r.best!.stats!) })
  if (loudest)
    cards.push({
      key: 'comms',
      p: loudest,
      icon: MicIcon,
      label: t('awardComms'),
      text: t('awardCommsText', { n: Math.round((100 * loudest.voice) / talk) }),
      sub: [t('awardTalkers', { n: roster.players.filter(p => p.voice > 0).length, m: roster.players.length })],
    })
  if (!cards.length) return null
  return (
    <section className="awards">
      {cards.map(c => {
        const Icon = c.icon
        return (
          <div key={c.key} className={`award a-${c.key} ${roster.teams[c.p.team]?.side === 3 ? 'ct' : 't'}${c.key === 'mvp' ? ' star' : ''}`}>
            <span className="award-mark" aria-hidden="true">
              <Icon />
            </span>
            <span className="award-badge">{c.label}</span>
            <span className="award-face">
              <Avatar url={avatars[c.p.steamid]} name={c.p.name} />
              <span className="medal" aria-hidden="true">
                <Icon />
              </span>
            </span>
            <span className="award-name">{c.p.name}</span>
            <span className="award-text">{c.text}</span>
            <span className="award-sub">
              {c.sub.map(x => (
                <span key={x}>{x}</span>
              ))}
            </span>
          </div>
        )
      })}
    </section>
  )
}

const PERF_TILES: { key: 'ttd' | 'reaction' | 'placement'; unit: string; digits: number }[] = [
  { key: 'ttd', unit: 'ms', digits: 0 },
  { key: 'reaction', unit: 'ms', digits: 0 },
  { key: 'placement', unit: '°', digits: 1 },
]

function PerfTile(props: { kind: (typeof PERF_TILES)[number]; value: number | null; n: number; waiting: boolean }) {
  const { t, lang } = useUi()
  const { kind, value, n, waiting } = props
  return (
    <div className="tile">
      <div className="tile-label">{t(kind.key)}</div>
      <div className="tile-value" style={normTone(kind.key, '', value)}>
        {waiting ? <TabSpin on /> : fmt(value, kind.digits, lang)}
        {!waiting && value !== null && <small>{kind.unit}</small>}
      </div>
      {!waiting && <div className="tile-note">{t('samples', { n })}</div>}
    </div>
  )
}

const SHOT_TILES: { key: 'firstShot' | 'counterStrafe' | 'swings'; note: Key; of: (r: PerfRow) => [number, number] }[] = [
  { key: 'firstShot', note: 'firstShots', of: r => [r.first_hits, r.first] },
  { key: 'counterStrafe', note: 'rifleShots', of: r => [r.stopped, r.stops] },
  { key: 'swings', note: 'swingsNote', of: r => [r.swings_good, r.swings] },
]

function ShotTile(props: { kind: (typeof SHOT_TILES)[number]; row?: PerfRow; waiting: boolean }) {
  const { t, lang } = useUi()
  const { kind, row, waiting } = props
  const [part, n] = row ? kind.of(row).map(v => v ?? 0) : [0, 0]
  const value = n > 0 ? (100 * part) / n : null
  return (
    <div className="tile">
      <div className="tile-label">{t(kind.key)}</div>
      <div className="tile-value" style={normTone(kind.key, '%', value)}>
        {waiting ? <TabSpin on /> : fmt(value, 1, lang)}
        {!waiting && value !== null && <small>%</small>}
      </div>
      {!waiting && <div className="tile-note">{t(kind.note, { n })}</div>}
    </div>
  )
}

const skinOrder = (a: SkinItem, b: SkinItem) =>
  Number(b.star && !b.gloves) - Number(a.star && !a.gloves) || Number(b.gloves) - Number(a.gloves) || b.rank - a.rank || a.weapon.localeCompare(b.weapon)

function Skins(props: { roster: Roster; teams: Player[][]; labels: string[]; avatars: Record<string, string>; msg: SkinsMsg | null; econ: Set<string> }) {
  const { t } = useUi()
  const { roster, teams, labels, avatars, msg, econ } = props
  const [pick, setPick] = useState<string | null>(null)
  const rows = useMemo(() => {
    const out: Record<string, { music: string; items: SkinItem[] }> = {}
    for (const p of msg?.players ?? []) out[p.steamid] = { music: p.music, items: [...p.items].sort(skinOrder) }
    return out
  }, [msg])
  const all = [...teams[0], ...teams[1]]
  const chosen = all.find(p => p.steamid === pick) ?? all.find(p => (rows[p.steamid]?.items.length ?? 0) > 0) ?? all[0]
  const row = chosen ? rows[chosen.steamid] : undefined
  const [sideFor, setSideFor] = useState<{ steamid: string; side: number } | null>(null)
  const side = sideFor && sideFor.steamid === chosen?.steamid ? sideFor.side : 0
  const [open, setOpen] = useState<SkinItem | null>(null)
  useEffect(() => setOpen(null), [chosen?.steamid, side])
  const onSide = (it: SkinItem, s: number) => !it.sides || (it.sides & s) !== 0
  const items = (row?.items ?? []).filter(it => !side || onSide(it, side))
  const cols = items.length > 12 ? 6 : items.length > 8 ? 5 : 4
  const lines = Math.max(1, Math.ceil(items.length / cols))
  return (
    <div className="skins">
      {open && items.includes(open) && (
        <SkinViewer items={items} index={items.indexOf(open)} econ={econ} onIndex={i => setOpen(items[i])} onClose={() => setOpen(null)} />
      )}
      <nav className="player-list" aria-label={t('tabSkins')}>
        {[0, 1].map(i => (
          <div key={i} className={`pl-group ${roster.teams[i]?.side === 3 ? 'ct' : 't'}`}>
            <div className="pl-team">{labels[i]}</div>
            {teams[i].map(p => (
              <button key={p.slot} className={`pl-row sk-row${chosen?.slot === p.slot ? ' on' : ''}`} onClick={() => setPick(p.steamid)}>
                <Avatar url={avatars[p.steamid]} name={p.name} />
                <span className="pl-name">{p.name || t('slot', { n: p.slot })}</span>
                <span className="pl-kda">{msg ? rows[p.steamid]?.items.length ?? 0 : ''}</span>
              </button>
            ))}
          </div>
        ))}
      </nav>
      {chosen && (
        <section className={`sk-side ${roster.teams[chosen.team]?.side === 3 ? 'ct' : 't'}`}>
          <div className="stats-head sk-head">
            <Avatar url={avatars[chosen.steamid]} name={chosen.name} />
            <h2>{chosen.name}</h2>
            <span>{row?.music ? t('skinsMusic', { name: row.music }) : ''}</span>
            <div className="sk-sides" role="tablist">
              {[0, 1, 2].map(s => (
                <button
                  key={s}
                  role="tab"
                  aria-selected={side === s}
                  className={`${s === 1 ? 't' : s === 2 ? 'ct' : 'all'}${side === s ? ' on' : ''}`}
                  onClick={() => chosen && setSideFor({ steamid: chosen.steamid, side: s })}
                >
                  {t(s === 1 ? 'skinsSideT' : s === 2 ? 'skinsSideCt' : 'skinsSideAll')}
                  <small>{(row?.items ?? []).filter(it => !s || onSide(it, s)).length}</small>
                </button>
              ))}
            </div>
          </div>
          {!msg ? (
            <div className="sk-empty">{t('skinsWait')}</div>
          ) : msg.err ? (
            <div className="notice bad">{msg.err}</div>
          ) : items.length === 0 ? (
            <div className="sk-empty">{t((row?.items.length ?? 0) > 0 ? 'skinsNoneSide' : 'skinsNone')}</div>
          ) : (
            <div className="sk-grid" style={{ gridTemplateColumns: `repeat(${cols}, minmax(0, 210px))`, gridTemplateRows: `repeat(${lines}, minmax(0, 220px))` }}>
              {items.map((it, k) => (
                <SkinCard key={k} it={it} econ={econ} on={open === it} side={side ? 0 : it.sides} onOpen={() => setOpen(open === it ? null : it)} />
              ))}
            </div>
          )}
        </section>
      )}
    </div>
  )
}

function EconImg(props: { src: string; econ: Set<string>; className?: string; alt?: string }) {
  const { src, econ, className, alt } = props
  const [bad, setBad] = useState(false)
  const made = econ.has(src)
  useEffect(() => setBad(false), [made])
  if (!src || bad) return <span className={className} />
  return <img key={made ? 'made' : 'old'} className={className} src={`https://cs2voice-maps.example/${src}`} alt={alt ?? ''} onError={() => setBad(true)} />
}

const WEAR_STEPS = [0, 0.07, 0.15, 0.38, 0.45, 1]

const ZOOM_MAX = 3

function ZoomStage(props: { src: string; econ: Set<string>; alt: string }) {
  const { t } = useUi()
  const { src, econ, alt } = props
  const box = useRef<HTMLDivElement>(null)
  const [z, setZ] = useState({ s: 1, x: 0, y: 0 })
  const [dragging, setDragging] = useState(false)
  const drag = useRef<{ px: number; py: number; x: number; y: number } | null>(null)
  const clampPan = (s: number, x: number, y: number) => {
    const r = box.current?.getBoundingClientRect()
    if (!r || s <= 1) return { s: 1, x: 0, y: 0 }
    const mx = ((s - 1) * r.width) / 2
    const my = ((s - 1) * r.height) / 2
    return { s, x: Math.max(-mx, Math.min(mx, x)), y: Math.max(-my, Math.min(my, y)) }
  }
  const zoomAt = (cx: number | null, cy: number | null, next: (s: number) => number) => {
    const r = box.current?.getBoundingClientRect()
    if (!r) return
    const ox = cx === null ? 0 : cx - r.left - r.width / 2
    const oy = cy === null ? 0 : cy - r.top - r.height / 2
    setZ(p => {
      const s = Math.max(1, Math.min(ZOOM_MAX, next(p.s)))
      const k = s / p.s
      return clampPan(s, ox - (ox - p.x) * k, oy - (oy - p.y) * k)
    })
  }
  useEffect(() => {
    const el = box.current
    if (!el) return
    const wheel = (e: WheelEvent) => {
      e.preventDefault()
      zoomAt(e.clientX, e.clientY, s => s * (e.deltaY < 0 ? 1.15 : 1 / 1.15))
    }
    el.addEventListener('wheel', wheel, { passive: false })
    return () => el.removeEventListener('wheel', wheel)
  }, [])
  return (
    <div className="sv-stage">
      <div
        ref={box}
        className={`sv-canvas${z.s > 1 ? ' zoomed' : ''}${dragging ? ' dragging' : ''}`}
        onPointerDown={e => {
          if (z.s <= 1) return
          drag.current = { px: e.clientX, py: e.clientY, x: z.x, y: z.y }
          setDragging(true)
          e.currentTarget.setPointerCapture(e.pointerId)
        }}
        onPointerMove={e => {
          const d = drag.current
          if (!d) return
          setZ(p => clampPan(p.s, d.x + e.clientX - d.px, d.y + e.clientY - d.py))
        }}
        onPointerUp={() => {
          drag.current = null
          setDragging(false)
        }}
        onDoubleClick={e => (z.s > 1 ? setZ({ s: 1, x: 0, y: 0 }) : zoomAt(e.clientX, e.clientY, () => 2.2))}
      >
        <div className="sv-pic" style={{ transform: `translate(${z.x}px, ${z.y}px) scale(${z.s})` }}>
          <EconImg src={src} econ={econ} className="sv-img" alt={alt} />
        </div>
      </div>
      <div className="sv-zoom">
        <button onClick={() => zoomAt(null, null, s => s / 1.4)} aria-label={t('skinsZoomOut')} disabled={z.s <= 1}>
          <MinusIcon />
        </button>
        <button className="sv-pct" onClick={() => setZ({ s: 1, x: 0, y: 0 })} aria-label={t('skinsZoomReset')}>
          {Math.round(z.s * 100)}%
        </button>
        <button onClick={() => zoomAt(null, null, s => s * 1.4)} aria-label={t('skinsZoomIn')} disabled={z.s >= ZOOM_MAX}>
          <PlusIcon />
        </button>
      </div>
    </div>
  )
}

const LOOKS = ['FN · MW', 'FT', 'WW · BS']

function SkinViewer(props: { items: SkinItem[]; index: number; econ: Set<string>; onIndex: (i: number) => void; onClose: () => void }) {
  const { t } = useUi()
  const { items, index, econ, onIndex, onClose } = props
  const it = items[index]
  const own = it.variants.length ? (it.wear < 0.15 ? 0 : it.wear < 0.38 ? 1 : 2) : -1
  const [look, setLook] = useState(own)
  const [sticker, setSticker] = useState<number | null>(null)
  useEffect(() => {
    setLook(own)
    setSticker(null)
    if (it.variants.length) send({ type: 'econ_want', icons: it.variants.map(v => v.icon) })
  }, [index])
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose()
      else if (e.key === 'ArrowLeft') onIndex((index + items.length - 1) % items.length)
      else if (e.key === 'ArrowRight') onIndex((index + 1) % items.length)
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [index, items.length, onClose, onIndex])
  const src = sticker !== null ? it.stickers[sticker].img : look >= 0 && it.variants[look] ? it.variants[look].img : it.img
  const name = sticker !== null ? it.stickers[sticker].name : `${it.weapon} ${it.skin}`
  return (
    <div className="sv" role="dialog" aria-modal="true" aria-label={name} style={{ '--rarity': it.color || 'var(--faint)' } as React.CSSProperties}>
      <div className="sv-main">
        <ZoomStage key={src} src={src} econ={econ} alt={name} />
        {items.length > 1 && (
          <>
            <button className="sv-nav prev" onClick={() => onIndex((index + items.length - 1) % items.length)} aria-label={t('skinsPrev')}>
              <ChevronLeftIcon />
            </button>
            <button className="sv-nav next" onClick={() => onIndex((index + 1) % items.length)} aria-label={t('skinsNext')}>
              <ChevronRightIcon />
            </button>
          </>
        )}
        <div className="sv-bottom">
          {sticker !== null ? (
            <button className="sv-back" onClick={() => setSticker(null)}>
              <ChevronLeftIcon />
              {t('skinsBack')}
            </button>
          ) : it.variants.length > 0 ? (
            <div className="sv-looks" role="tablist" aria-label={t('skinsLooks')}>
              {it.variants.map((v, k) => (
                <button key={v.icon} role="tab" aria-selected={look === k} className={look === k ? 'on' : ''} onClick={() => setLook(k)}>
                  <EconImg src={v.img} econ={econ} className="sv-look-img" />
                  <span>
                    {LOOKS[k]}
                    {k === own && <small>{t('skinsThisOne')}</small>}
                  </span>
                </button>
              ))}
            </div>
          ) : (
            <span />
          )}
          <span className="sv-count">
            {index + 1} / {items.length}
          </span>
        </div>
      </div>
      <aside className="sv-info">
        <button className="sk-close" onClick={onClose} aria-label={t('skinsClose')}>
          <CloseIcon />
        </button>
        <div className="sk-title">
          <span>
            {it.star ? '★ ' : ''}
            {it.weapon}
          </span>
          <h3>{it.skin || it.weapon}</h3>
          {it.rarity && <b>{it.rarity}</b>}
        </div>
        {it.paint > 0 && (
          <div className="sk-wear">
            <div className="sk-wear-head">
              <span>{it.wear_name}</span>
              <span>
                {t('skinsFloat')} {it.wear.toFixed(6)}
              </span>
            </div>
            <div className="sk-bar">
              {WEAR_STEPS.slice(1).map((end, k) => (
                <span key={k} className={`w${k}`} style={{ width: `${(end - WEAR_STEPS[k]) * 100}%` }} />
              ))}
              <i style={{ left: `${Math.min(1, Math.max(0, it.wear)) * 100}%` }} />
            </div>
          </div>
        )}
        <dl className="sk-list">
          {it.paint > 0 && (
            <>
              <dt>{t('skinsPatternLabel')}</dt>
              <dd>{it.seed}</dd>
            </>
          )}
          {it.st >= 0 && (
            <>
              <dt>StatTrak™</dt>
              <dd className="sk-st">{it.st}</dd>
            </>
          )}
          {it.tag && (
            <>
              <dt>{t('skinsTag')}</dt>
              <dd className="sk-tag">"{it.tag}"</dd>
            </>
          )}
        </dl>
        <div className="sv-sticks">
          <span className="sk-sub">{it.stickers.length ? t('skinsStickers') : t('skinsNoStickers')}</span>
          {it.stickers.map((s, k) => (
            <button key={k} className={sticker === k ? 'on' : ''} onClick={() => setSticker(sticker === k ? null : k)}>
              <EconImg src={s.img} econ={econ} className="sk-stick" alt={s.name} />
              <span>{s.name}</span>
            </button>
          ))}
        </div>
      </aside>
    </div>
  )
}

function SkinCard(props: { it: SkinItem; econ: Set<string>; on: boolean; side: number; onOpen: () => void }) {
  const { t } = useUi()
  const { it, econ, on, side, onOpen } = props
  return (
    <button className={`sk-card${on ? ' on' : ''}`} style={{ '--rarity': it.color || 'var(--faint)' } as React.CSSProperties} onClick={onOpen}>
      <div className="sk-art">
        <EconImg src={it.img} econ={econ} className="sk-img" />
        {(side === 1 || side === 2) && <span className={`sk-side-mark ${side === 1 ? 't' : 'ct'}`}>{side === 1 ? 'T' : 'CT'}</span>}
        {it.stickers.length > 0 && (
          <div className="sk-stickers">
            {it.stickers.map((s, k) => (
              <EconImg key={k} src={s.img} econ={econ} className="sk-sticker" alt={s.name} />
            ))}
          </div>
        )}
      </div>
      <div className="sk-name">
        <span className="sk-weapon">
          {it.star ? '★ ' : ''}
          {it.weapon}
        </span>
        <b>{it.skin || it.weapon}</b>
      </div>
      <div className="sk-meta">
        <i />
        {it.wear_name && <span>{it.wear_name}</span>}
        {it.paint > 0 && <span className="sk-num">{it.wear.toFixed(4)}</span>}
        {it.st >= 0 && <span className="sk-st">{t('skinsStattrak', { n: it.st })}</span>}
      </div>
    </button>
  )
}

function Crosshairs(props: { roster: Roster; teams: Player[][]; labels: string[]; avatars: Record<string, string>; art: boolean; copied: string }) {
  const { roster, teams, labels, avatars, art, copied } = props
  return (
    <div className="xh">
      {[0, 1].map(i => (
        <section key={i} className={`xh-team ${roster.teams[i]?.side === 3 ? 'ct' : 't'}`}>
          <h3>{labels[i]}</h3>
          <div className="xh-grid">
            {teams[i].map(p => (
              <CrosshairCard key={p.slot} p={p} avatar={avatars[p.steamid]} map={art ? roster.map : ''} copied={copied} />
            ))}
          </div>
        </section>
      ))}
    </div>
  )
}

function CrosshairCard(props: { p: Player; avatar?: string; map: string; copied: string }) {
  const { t } = useUi()
  const { p, avatar, map, copied } = props
  const code = p.crosshair ?? ''
  const x = useMemo(() => (code ? decode(code) : null), [code])
  const ref = useRef<HTMLCanvasElement>(null)
  const view = useRef<HTMLDivElement>(null)
  const drawn = !!x && x.format !== 'classic'
  useLayoutEffect(() => {
    const cv = ref.current
    const box = view.current
    if (!cv || !box || !x || !drawn) return
    const shape = document.createElement('canvas')
    draw(shape, x)
    const paint = () => {
      const dpr = window.devicePixelRatio || 1
      const r = box.getBoundingClientRect()
      const w = Math.round(r.width * dpr)
      const h = Math.round(r.height * dpr)
      cv.width = w
      cv.height = h
      cv.style.width = `${w / dpr}px`
      cv.style.height = `${h / dpr}px`
      const ctx = cv.getContext('2d')
      if (!ctx) return
      ctx.imageSmoothingEnabled = false
      ctx.clearRect(0, 0, w, h)
      const s = shape.width * Math.max(1, Math.round(dpr))
      ctx.drawImage(shape, Math.floor((w - s) / 2), Math.floor((h - s) / 2), s, s)
    }
    paint()
    const ro = new ResizeObserver(paint)
    ro.observe(box)
    return () => ro.disconnect()
  }, [x, drawn])
  const px = (n: number) => (x?.format === 'classic' ? fmtNum(n) : `${n} px`)
  return (
    <article className="xh-card">
      <div className="xh-head">
        <Avatar url={avatar} name={p.name} />
        <span>{p.name || t('slot', { n: p.slot })}</span>
      </div>
      <div ref={view} className="xh-view" style={map ? { backgroundImage: `url(https://cs2voice-maps.example/${map}.png)` } : undefined}>
        {drawn ? (
          <canvas ref={ref} />
        ) : (
          <span className="xh-none">{!code ? t('xhNone') : x ? t('xhClassic') : t('xhUnknown')}</span>
        )}
      </div>
      {x && (
        <div className="xh-facts">
          <div className="xh-style">
            <i className="xh-swatch" style={{ background: `rgba(${x.color[0]},${x.color[1]},${x.color[2]},${x.color[3] / 255})` }} />
            <span>{t(`xhStyle${x.style}` as Key)}</span>
          </div>
          <dl className="xh-nums">
            <div>
              <dt>{t('xhThickness')}</dt>
              <dd>{px(x.thickness)}</dd>
            </div>
            <div>
              <dt>{t('xhLength')}</dt>
              <dd>{px(x.length)}</dd>
            </div>
            <div>
              <dt>{t('xhGap')}</dt>
              <dd>{px(x.gap)}</dd>
            </div>
          </dl>
          <div className="xh-more">
            {[t(`xhOutline${Math.min(2, x.outlineMode)}` as Key), x.dot ? t('xhDot') : '', x.screen > 0 ? t('xhScreen', { n: x.screen }) : '']
              .filter(Boolean)
              .join(', ')}
          </div>
        </div>
      )}
      {code && (
        <div className="xh-code">
          <code>{code}</code>
          <button className={`copy${copied === code ? ' done' : ''}`} onClick={() => send({ type: 'copy', text: code })}>
            {copied === code ? t('copied') : t('copy')}
          </button>
        </div>
      )}
      {x && x.format !== 'cs2' && <span className="tag mute xh-old">{t('xhOld')}</span>}
    </article>
  )
}

const fmtNum = (n: number) => (Math.round(n * 10) / 10).toString()

function Tile(props: { measure: Measure; stats: Stats; pool: Stats[] }) {
  const { t, lang } = useUi()
  const { measure, stats, pool } = props
  const value = measure.of(stats)
  const values = pool.map(measure.of).filter((v): v is number => v !== null)
  const mid = median(values)
  const W = 132
  const H = 34
  const COUNT = 22
  const hist = values.length > 1 ? bins(values, COUNT) : null
  const peak = hist ? Math.max(...hist.counts) : 1
  const x = (v: number) => (hist ? ((v - hist.lo) / (hist.hi - hist.lo || 1)) * W : 0)
  const label = t(measure.key)
  return (
    <div className="tile">
      <div className="tile-label">{label}</div>
      <div className="tile-value" style={measure.key === 'rating' ? ratingTone(value) : normTone(measure.key, measure.unit, value)}>
        {fmt(value, measure.digits, lang)}
        {value !== null && measure.unit && <small>{measure.unit}</small>}
      </div>
      {hist && (
        <svg className="hist" viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" aria-hidden="true">
          {hist.counts.map((c, i) => {
            const h = c ? Math.max(2, (c / peak) * (H - 2)) : 1
            const own = value !== null && hist.at(value) === i
            return <rect key={i} className={own ? 'own' : ''} x={(i * W) / COUNT + 0.5} y={H - h} width={W / COUNT - 1} height={h} />
          })}
          {mid !== null && <line className="mid" x1={x(mid)} x2={x(mid)} y1={0} y2={H} />}
        </svg>
      )}
    </div>
  )
}

const FACEIT_COLORS = ['transparent', '#f1f1f1', '#05ff00', '#05ff00', '#ffc700', '#ffc700', '#ffc700', '#ffc700', '#ff4b00', '#ff4b00', '#ef0000']
const FACEIT_ARC = 'M 6.5 18.4 A 8.4 8.4 0 1 1 17.5 18.4'

function FaceitLevel(props: { level: number; elo: number; title: string }) {
  const id = 'fl' + useId().replace(/:/g, '')
  const level = Math.min(10, props.level)
  return (
    <span className="faceit-level" title={props.title}>
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <defs>
          <radialGradient id={id} cx="0.5" cy="0.5" r="0.55">
            <stop offset="0.4" stopColor="#5d5d5d" />
            <stop offset="1" stopColor="#242424" />
          </radialGradient>
        </defs>
        <circle cx="12" cy="12" r="12" fill="#121212" />
        <path d={FACEIT_ARC} stroke={`url(#${id})`} strokeWidth="2.4" fill="none" />
        <path
          d={FACEIT_ARC}
          stroke={FACEIT_COLORS[level]}
          strokeWidth="2.4"
          fill="none"
          pathLength={10}
          strokeDasharray="10.1 10000"
          strokeDashoffset={level >= 10 ? 0 : 10 - 0.99 * level}
        />
        <text x="12" y="12.4" textAnchor="middle" dominantBaseline="central">
          {level}
        </text>
      </svg>
      {props.elo > 0 && <span className="elo">{props.elo}</span>}
    </span>
  )
}

const CHALLENGER_TOP = 1000
const CHALLENGER_COLORS = ['#e80128', '#ffd336', '#def5ff', '#ff7236']

function FaceitRank(props: { rank: number; elo: number; title: string }) {
  const color = CHALLENGER_COLORS[props.rank <= 3 ? props.rank : 0]
  return (
    <span className="faceit-level" title={props.title}>
      <span className={`faceit-rank${props.rank <= 3 ? ' podium' : ''}`} style={{ background: color }}>
        #{props.rank}
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <circle cx="12" cy="12" r="12" fill="#121212" />
          <path d={FACEIT_ARC} stroke={color} strokeWidth="2.4" fill="none" />
          <circle cx="12" cy="12" r="3.2" fill={color} />
        </svg>
      </span>
      {props.elo > 0 && <span className="elo">{props.elo}</span>}
    </span>
  )
}

function FaceitBadge(props: { level: number; elo: number; rank: number }) {
  const { t } = useUi()
  const { level, elo, rank } = props
  if (rank > 0 && rank <= CHALLENGER_TOP) return <FaceitRank rank={rank} elo={elo} title={t('faceitRank', { rank, elo })} />
  if (level > 0) return <FaceitLevel level={level} elo={elo} title={t('faceitLevel', { level, elo })} />
  return null
}

function Badges(props: { profile?: Profile; mute?: number }) {
  const { t } = useUi()
  const p = props.profile
  const mute = props.mute ?? 0
  if (!p && !mute) return null
  return (
    <span className="badges">
      {mute > 0 && (
        <span className="badge icon" title={t(mute & 2 ? 'muteAbuseTip' : 'muteTip')}>
          <MicOffIcon />
        </span>
      )}
      {p?.vac && (
        <span className="badge bad">
          {t('vacBadge')}
        </span>
      )}
      {p?.limited && (
        <span className="badge icon" title={t('limitedTip')}>
          <UserMinusIcon />
        </span>
      )}
      {p?.hidden && (
        <span className="badge icon" title={t('hiddenTip')}>
          <LockIcon />
        </span>
      )}
    </span>
  )
}


function Empty(props: { loading: boolean }) {
  const { t } = useUi()
  if (props.loading) return <div className="empty" />
  return (
    <div className="empty">
      <h1>{t('noDemos')}</h1>
      <p>{t('noDemosText')}</p>
      <button className="copy" onClick={() => send({ type: 'add_folder' })}>
        {t('addFolder')}
      </button>
    </div>
  )
}
