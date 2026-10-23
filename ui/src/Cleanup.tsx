import { useEffect, useState } from 'react'
import { send, type CleanupCfg, type Folder } from './bridge'
import { DAY, bytesText, type CleanupRow, type Reason } from './cleanplan'
import { useUi } from './common'
import { plural, type T } from './i18n'
import { mapName } from './voice'

const DAYS = [3, 7, 14, 30]

const total = (rows: CleanupRow[]) => rows.reduce((n, r) => n + r.bytes, 0)

const count = (n: number, t: T) => plural(t, n, 'filesOne', 'filesFew', 'filesMany')
const files = (rows: CleanupRow[]) => rows.reduce((n, r) => n + r.files.length, 0)

const set = (key: string, value: string) => send({ type: 'set', key, value })

export function Check(props: { on: boolean; onChange: (on: boolean) => void; label: string; note?: string }) {
  return (
    <button className="cl-opt" role="checkbox" aria-checked={props.on} onClick={() => props.onChange(!props.on)}>
      <span className={`cl-box${props.on ? ' on' : ''}`} />
      <span>
        {props.label}
        {props.note && <small>{props.note}</small>}
      </span>
    </button>
  )
}

export function CleanupSettings(props: { cfg: CleanupCfg; folders: Folder[]; plan: CleanupRow[]; onReview: () => void }) {
  const { t, lang } = useUi()
  const { cfg, folders, plan, onReview } = props
  const modes: { id: CleanupCfg['mode']; name: Parameters<typeof t>[0] }[] = [
    { id: 'off', name: 'cleanupOff' },
    { id: 'ask', name: 'cleanupAsk' },
    { id: 'auto', name: 'cleanupAuto' },
  ]
  return (
    <section className="setting">
      <div className="setting-head">
        <h2>{t('cleanup')}</h2>
        <p>{t('cleanupText')}</p>
      </div>
      <div className="cl-body">
        <div className="choices" role="radiogroup" aria-label={t('cleanup')}>
          {modes.map(m => (
            <button key={m.id} role="radio" aria-checked={cfg.mode === m.id} className={`choice${cfg.mode === m.id ? ' on' : ''}`} onClick={() => set('cleanup', m.id)}>
              {t(m.name)}
            </button>
          ))}
        </div>
        {cfg.mode !== 'off' && (
          <div className="cl-grid">
            <div className="cl-col">
              <h3>{t('cleanupOld')}</h3>
              <div className="cl-days">
                <span>{t('cleanupNotOpened')}</span>
                {DAYS.map(d => (
                  <button key={d} className={cfg.days === d ? 'on' : ''} onClick={() => set('cleanup_days', String(d))}>
                    {t('cleanupDays', { n: d })}
                  </button>
                ))}
              </div>
              <p className="cl-note">{t('cleanupOldNote')}</p>
              <h3>{t('cleanupAlso')}</h3>
              <Check on={cfg.copies} onChange={on => set('cleanup_copies', on ? '1' : '0')} label={t('cleanupCopies')} note={t('cleanupCopiesNote')} />
              <Check on={cfg.broken} onChange={on => set('cleanup_broken', on ? '1' : '0')} label={t('cleanupBroken')} note={t('cleanupBrokenNote')} />
              <Check on={cfg.mute} onChange={on => set('cleanup_mute', on ? '1' : '0')} label={t('cleanupMute')} />
            </div>
            <div className="cl-col">
              <h3>{t('cleanupHow')}</h3>
              <div className="cl-days">
                <button className={cfg.method === 'recycle' ? 'on' : ''} onClick={() => set('cleanup_method', 'recycle')}>
                  {t('cleanupRecycle')}
                </button>
                <button className={cfg.method === 'delete' ? 'on danger' : ''} onClick={() => set('cleanup_method', 'delete')}>
                  {t('cleanupDeleteGo')}
                </button>
              </div>
              <h3>{t('cleanupFolders')}</h3>
              {folders.map(f => (
                <Check key={f.path} on={!cfg.skip.includes(f.path)} onChange={on => send({ type: 'cleanup_folder', path: f.path, on })} label={f.path} />
              ))}
              <p className="cl-note">{t('cleanupNever')}</p>
            </div>
          </div>
        )}
        {cfg.mode !== 'off' && (
          <div className="cl-preview">
            {plan.length ? (
              <>
                <b>{count(files(plan), t)}</b>
                <span>{bytesText(total(plan), lang)}</span>
                <small>{t(cfg.mode === 'auto' ? 'cleanupAutoNote' : 'cleanupNow')}</small>
                <button onClick={onReview}>{t('cleanupReview')}</button>
              </>
            ) : (
              <small>{t('cleanupNothing')}</small>
            )}
          </div>
        )}
      </div>
    </section>
  )
}

const reasons = (plan: CleanupRow[]) => {
  const n = (r: Reason) => plan.filter(x => x.reason === r).length
  return { copy: n('copy'), old: n('old'), broken: n('broken'), mute: n('mute') }
}

export function CleanupCard(props: { plan: CleanupRow[]; cfg: CleanupCfg; onReview: () => void; onSettings: () => void }) {
  const { t, lang } = useUi()
  const { plan, cfg, onReview, onSettings } = props
  const c = reasons(plan)
  const parts = [
    c.old ? t('cleanupCardOld', { n: c.old, d: cfg.days }) : '',
    c.copy ? t('cleanupCardCopy', { n: c.copy }) : '',
    c.broken ? t('cleanupCardBroken', { n: c.broken }) : '',
    c.mute ? t('cleanupCardMute', { n: c.mute }) : '',
  ].filter(Boolean)
  return (
    <div className="cl-card">
      <b>
        {count(files(plan), t)}, {bytesText(total(plan), lang)}
      </b>
      <span>{parts.join(', ')}</span>
      <div className="row">
        <button className="go" onClick={onReview}>
          {t('cleanupReview')}
        </button>
        <button onClick={() => set('snooze', String(Math.floor(Date.now() / 1000) + DAY))}>{t('later')}</button>
        <button className="cfg" onClick={onSettings}>
          {t('settings')}
        </button>
      </div>
    </div>
  )
}

export function CleanupReview(props: { plan: CleanupRow[]; cfg: CleanupCfg; onClose: () => void }) {
  const { t, lang } = useUi()
  const { plan, cfg, onClose } = props
  const [off, setOff] = useState<Set<string>>(new Set())
  useEffect(() => {
    if (!plan.length) onClose()
  }, [plan, onClose])
  const picked = plan.filter(r => !off.has(r.key))
  const toggle = (key: string) => {
    const next = new Set(off)
    if (next.has(key)) next.delete(key)
    else next.add(key)
    setOff(next)
  }
  const groups: { reason: Reason; title: string }[] = [
    { reason: 'copy', title: t('cleanupSecCopy') },
    { reason: 'old', title: t('cleanupSecOld', { d: cfg.days }) },
    { reason: 'broken', title: t('cleanupSecBroken') },
    { reason: 'mute', title: t('cleanupSecMute') },
  ]
  const label = (r: CleanupRow) => {
    const ro = r.roster
    if (!ro || !ro.map) return { map: r.files[0].name.replace(/\.dem(\.zst)?$/i, ''), score: '' }
    return { map: mapName(ro.map, t), score: ro.teams.length === 2 && !ro.partial ? `${ro.teams[0].score} : ${ro.teams[1].score}` : '' }
  }
  const go = (method: CleanupCfg['method']) => {
    send({ type: 'cleanup', paths: picked.flatMap(r => r.files.map(f => f.path)), method })
    onClose()
  }
  return (
    <div className="modal" onMouseDown={onClose}>
      <div className="cl-review" onMouseDown={e => e.stopPropagation()} role="dialog" aria-label={t('cleanup')}>
        <h2>{t('cleanupTitle', { files: count(files(plan), t) })}</h2>
        <div className="cl-rows">
          {groups.map(g => {
            const rows = plan.filter(r => r.reason === g.reason)
            if (!rows.length) return null
            return (
              <div key={g.reason} className="cl-group">
                <div className="cl-sec">{g.title}</div>
                {rows.map(r => {
                  const l = label(r)
                  const on = !off.has(r.key)
                  return (
                    <div key={r.key} className={`cl-row${on ? '' : ' off'}`} role="checkbox" aria-checked={on} onClick={() => toggle(r.key)}>
                      <span className={`cl-box${on ? ' on' : ''}`} />
                      <span className="map">
                        {l.map}
                        <small>{l.score}</small>
                      </span>
                      <span className="file">
                        {r.files.map(f => f.name).join(' + ')}
                        <small>
                          {r.reason === 'copy' && r.stays
                            ? t('cleanupStays', { name: r.stays.name })
                            : r.files[0].opened
                              ? t('cleanupOpened', { d: r.days })
                              : t('cleanupNeverOpened', { d: r.days })}
                        </small>
                      </span>
                      <span className="size">{bytesText(r.bytes, lang)}</span>
                      <button
                        className="keep"
                        onClick={e => {
                          e.stopPropagation()
                          send({ type: 'keep', paths: r.files.map(f => f.path), on: true })
                        }}
                      >
                        {t('keep')}
                      </button>
                    </div>
                  )
                })}
              </div>
            )
          })}
        </div>
        <div className="cl-foot">
          <small>{t('cleanupPicked', { n: files(picked), all: files(plan) })}</small>
          <button onClick={onClose}>{t('cancel')}</button>
          <button className="go" disabled={!picked.length} onClick={() => go('recycle')}>
            {t('cleanupRecycleGo', { files: count(files(picked), t), size: bytesText(total(picked), lang) })}
          </button>
          <button className="purge" disabled={!picked.length} onClick={() => go('delete')}>
            {t('cleanupDeleteGo')}
          </button>
        </div>
      </div>
    </div>
  )
}
