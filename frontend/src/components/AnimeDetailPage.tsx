import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Descriptions, Input, List, Modal, Space, Spin, Typography } from 'antd'
import { ApiError, previewAnimeMerge, mergeAnime, bindBangumi, editAnime, editAnimeMedia, getAnimeDetail, getAuditLogs, playMedia, refreshAnimeCover, searchBangumi } from '../api/client'
import type { AnimeMergePreview, AnimeDetail, AuditLog, BangumiCandidate, BangumiSearch, Media } from '../api/types'
import { PlaybackPlayerSelect } from './PlaybackPlayerSelect'

type Props = { animeId: number; onBack: () => void; onOpenAnime?: (id: number) => void }
const errorText = (error: unknown) => error instanceof Error ? error.message : '请求失败'
const mediaPageSize = 200

export function AnimeDetailPage({ animeId, onBack, onOpenAnime }: Props) {
  // 当前 animeId 是详情操作上下文，媒体分页、Bangumi 绑定与播放均围绕该条目执行。
  const [detail, setDetail] = useState<AnimeDetail | null>(null)
  const [edit, setEdit] = useState<Pick<AnimeDetail, 'displayTitle' | 'season' | 'coverUrl'> | null>(null)
  const [episodeEdit, setEpisodeEdit] = useState<Pick<Media, 'id' | 'episodeNumber' | 'episodeType'> | null>(null)
  const [saving, setSaving] = useState(false)
  const [editError, setEditError] = useState('')
  const [media, setMedia] = useState<Media[]>([])
  const [nextMediaOffset, setNextMediaOffset] = useState<number | null>(null)
  const [audits, setAudits] = useState<AuditLog[]>([])
  const [loading, setLoading] = useState(true)
  const [moreBusy, setMoreBusy] = useState(false)
  const [error, setError] = useState('')
  const [remoteError, setRemoteError] = useState('')
  const [query, setQuery] = useState('')
  const [results, setResults] = useState<BangumiSearch | null>(null)
  const [candidate, setCandidate] = useState<BangumiCandidate | null>(null)
  const [binding, setBinding] = useState(false)
  const [searching, setSearching] = useState(false)
  const [mergeSubject, setMergeSubject] = useState<number | null>(null)
  const [mergePreview, setMergePreview] = useState<AnimeMergePreview | null>(null)
  const [mergeBusy, setMergeBusy] = useState(false)
  const [refreshingCover, setRefreshingCover] = useState(false)
  const [coverError, setCoverError] = useState('')
  const [playingId, setPlayingId] = useState<number | null>(null)
  const [playError, setPlayError] = useState('')
  const [playNotice, setPlayNotice] = useState('')
  const [playerId, setPlayerId] = useState('')
  const generation = useRef(0)
  const searchGeneration = useRef(0)
  const mediaGeneration = useRef(0)

  async function reload(id: number, current: number) {
    const anime = await getAnimeDetail(id, mediaPageSize, 0)
    if (current !== generation.current) return
    setDetail(anime); setMedia(anime.media); setNextMediaOffset(anime.nextMediaOffset)
    void getAuditLogs(20, 0, id).then((history) => { if (current === generation.current) setAudits(history.items) })
      .catch((cause) => { if (current === generation.current) setError(`审计记录：${errorText(cause)}`) })
  }
  useEffect(() => {
    const generationRef = generation
    const searchRef = searchGeneration
    const mediaRef = mediaGeneration
    const current = ++generationRef.current
    setPlayerId(''); setSearching(false); setMergeSubject(null); setMergePreview(null); setMergeBusy(false)
    setEdit(null); setEpisodeEdit(null); setSaving(false); setEditError('')
    searchRef.current++
    mediaRef.current++
    setDetail(null); setMedia([]); setAudits([]); setMoreBusy(false); setBinding(false); setRefreshingCover(false); setCoverError(''); setPlayingId(null); setPlayError(''); setPlayNotice(''); setResults(null); setCandidate(null); setError(''); setRemoteError(''); setLoading(true)
    void reload(animeId, current).catch((cause) => { if (current === generation.current) setError(errorText(cause)) })
      .finally(() => { if (current === generation.current) setLoading(false) })
    return () => { if (generationRef.current === current) generationRef.current++; searchRef.current++; mediaRef.current++ }
  }, [animeId])

  async function loadMore() {
    if (nextMediaOffset === null || moreBusy) return
    const offset = nextMediaOffset
    const current = generation.current
    const mediaCurrent = ++mediaGeneration.current
    setMoreBusy(true); setError('')
    try {
      const page = await getAnimeDetail(animeId, mediaPageSize, offset)
      if (current !== generation.current || mediaCurrent !== mediaGeneration.current) return
      setMedia((previous) => [...previous, ...page.media]); setNextMediaOffset(page.nextMediaOffset)
    } catch (cause) { if (current === generation.current && mediaCurrent === mediaGeneration.current) setError(errorText(cause)) }
    finally { if (current === generation.current && mediaCurrent === mediaGeneration.current) setMoreBusy(false) }
  }
  async function saveEdit() {
    if (saving || (!edit && !episodeEdit)) return
    const current = generation.current
    setSaving(true); setEditError('')
    try {
      if (edit) await editAnime(animeId, { displayTitle: edit.displayTitle.trim(), season: edit.season.trim(), coverUrl: edit.coverUrl.trim() })
      else if (episodeEdit) await editAnimeMedia(episodeEdit.id, { episodeNumber: episodeEdit.episodeNumber.trim(), episodeType: episodeEdit.episodeType })
      if (current !== generation.current) return
      setEdit(null); setEpisodeEdit(null)
      mediaGeneration.current++; setMoreBusy(false)
      await reload(animeId, current)
    } catch (cause) { if (current === generation.current) setEditError(errorText(cause)) }
    finally { if (current === generation.current) setSaving(false) }
  }
  async function search() {
    const value = query.trim()
    if (new TextEncoder().encode(value).length < 1 || new TextEncoder().encode(value).length > 100) { setRemoteError('Bangumi 关键词须为 1–100 个 UTF-8 字节'); return }
    const current = ++searchGeneration.current
    setRemoteError(''); setResults(null); setSearching(true)
    try { const found = await searchBangumi(value); if (current === searchGeneration.current) setResults(found) }
    catch (cause) { if (current === searchGeneration.current) setRemoteError(errorText(cause)) }
    finally { if (current === searchGeneration.current) setSearching(false) }
  }
  async function confirmBinding() {
    if (!candidate || binding) return
    const subjectId = candidate.id
    const current = generation.current
    setBinding(true); setRemoteError(''); setMergeSubject(null); setMergePreview(null)
    try {
      await bindBangumi(animeId, subjectId)
      if (current !== generation.current) return
      mediaGeneration.current++
      setMoreBusy(false)
      setCandidate(null); setResults(null)
      await reload(animeId, current)
    } catch (cause) {
      if (current === generation.current) {
        if (cause instanceof ApiError && cause.code === 'bangumi_subject_in_use') {
          setCandidate(null); setMergeSubject(subjectId)
          setRemoteError(`该 Bangumi 条目已在番剧库中：${cause.targetTitle || '已有番剧'}。可预览后合并。`)
        } else setRemoteError(errorText(cause))
      }
    }
    finally { if (current === generation.current) setBinding(false) }
  }

  async function prepareMerge() {
    if (!mergeSubject || mergeBusy) return
    const current = generation.current
    setMergeBusy(true); setRemoteError('')
    try {
      const preview = await previewAnimeMerge(animeId, mergeSubject)
      if (current === generation.current) setMergePreview(preview)
    } catch (cause) { if (current === generation.current) setRemoteError(errorText(cause)) }
    finally { if (current === generation.current) setMergeBusy(false) }
  }
  async function confirmMerge() {
    if (!mergePreview || mergeBusy) return
    const current = generation.current
    setMergeBusy(true); setRemoteError('')
    try {
      const target = await mergeAnime(animeId, mergePreview.previewId)
      if (current !== generation.current) return
      setMergePreview(null)
      if (onOpenAnime) onOpenAnime(target.id)
      else onBack()
    } catch (cause) {
      if (current === generation.current) {
        setMergePreview(null)
        setRemoteError(cause instanceof ApiError && cause.code.startsWith('merge_preview_') ? '预览已失效，请重新预览后确认。' : errorText(cause))
      }
    } finally { if (current === generation.current) setMergeBusy(false) }
  }

  async function refreshCover() {
    if (refreshingCover || !detail?.bangumiSubjectId) return
    const current = generation.current
    setRefreshingCover(true); setCoverError('')
    try {
      await refreshAnimeCover(animeId)
      if (current === generation.current) await reload(animeId, current)
    } catch (cause) { if (current === generation.current) setCoverError(errorText(cause)) }
    finally { if (current === generation.current) setRefreshingCover(false) }
  }

  async function startPlayback(item: Media) {
    if (playingId !== null) return
    const current = generation.current
    setPlayingId(item.id); setPlayError(''); setPlayNotice('')
    try {
      const result = playerId ? await playMedia(item.id, playerId) : await playMedia(item.id)
      if (current === generation.current) setPlayNotice(`已启动${result.playerName || '所选播放器'}：${item.filename}`)
    } catch (cause) {
      if (current === generation.current) setPlayError(errorText(cause))
    } finally {
      if (current === generation.current) setPlayingId(null)
    }
  }

  return <Space className="detail-page" direction="vertical" size="middle" style={{ width: '100%' }}>
    <Button onClick={onBack}>返回番剧库</Button>
    {error && <Alert type="error" message={error} />}
    {loading && <Spin aria-label="正在加载详情" />}
    {detail && <>
      <Typography.Title level={3}>{detail.displayTitle}</Typography.Title>
      <Button onClick={() => { setEditError(''); setEdit({ displayTitle: detail.displayTitle, season: detail.season, coverUrl: detail.coverUrl }) }}>修改信息</Button>
      {editError && !edit && !episodeEdit && <Alert type="error" message={editError} />}
      {detail.coverUrl && <img src={detail.coverUrl} alt={`${detail.displayTitle} 封面`} style={{ width: 180, maxWidth: '100%', aspectRatio: '2 / 3', objectFit: 'cover' }} />}
      {detail.bangumiSubjectId && <Button loading={refreshingCover} onClick={() => void refreshCover()}>刮削封面</Button>}
      {coverError && <Alert type="warning" message={coverError} />}
      <Descriptions bordered column={1} items={[
        { key: 'season', label: '季', children: detail.season || '未标注' },
        { key: 'bangumi', label: 'Bangumi', children: detail.bangumiSubjectId ? <a href={`https://bgm.tv/subject/${detail.bangumiSubjectId}`} target="_blank" rel="noopener noreferrer">查看 Bangumi #{detail.bangumiSubjectId}</a> : <a href={`https://bgm.tv/subject_search/${encodeURIComponent(detail.displayTitle)}?cat=2`} target="_blank" rel="noopener noreferrer">在 Bangumi 搜索{detail.displayTitle}</a> },
        { key: 'aliases', label: '别名', children: detail.aliases.length ? detail.aliases.join('、') : '无' },
      ]} />
      <Typography.Title level={4}>媒体文件</Typography.Title>
      <PlaybackPlayerSelect value={playerId} onChange={setPlayerId} />
      {playError && <Alert type="error" message={playError} />}
      {playNotice && <Alert type="success" message={playNotice} />}
      <List dataSource={media} locale={{ emptyText: '暂无媒体文件' }} renderItem={(item) => {
        // One anime can contain releases whose parsed titles are romanized aliases.
        const title = detail.displayTitle.trim() || item.title.trim() || item.filename
        const number = item.episodeNumber.trim()
        const episode = item.episodeNumberSource === 'bangumi' && /^\d+$/.test(number) ? number.padStart(2, '0') : number
        return <List.Item key={item.id} actions={[<Button key="edit" aria-label={`修改集数 ${item.filename}`} onClick={() => { setEditError(''); setEpisodeEdit({ id: item.id, episodeNumber: item.episodeNumber, episodeType: item.episodeType }) }}>{'修改集数'}</Button>, <Button key="play" aria-label={`播放 ${item.filename}`} loading={playingId === item.id} disabled={playingId !== null && playingId !== item.id} onClick={() => void startPlayback(item)}>播放</Button>]}><Space direction="vertical"><Typography.Text strong>{title}{episode ? ` [${episode}]` : ''}</Typography.Text><Typography.Text type="secondary">{item.filename}</Typography.Text>{item.episodeNumberSource === 'bangumi' && item.rawEpisodeNumber !== item.episodeNumber && <Typography.Text type="secondary">原始集数 {item.rawEpisodeNumber} → 本季 {episode}</Typography.Text>}<Typography.Text type="secondary">{item.episodeType} {item.episodeNumber} · {item.status} · {item.origin}</Typography.Text></Space></List.Item>
      }} />
      {nextMediaOffset !== null && <Button loading={moreBusy} onClick={() => void loadMore()}>加载更多媒体</Button>}
      <Typography.Title level={4}>Bangumi 绑定</Typography.Title>
      <Space.Compact><Input aria-label="Bangumi 关键词" value={query} onChange={(event) => setQuery(event.target.value)} /><Button loading={searching} onClick={() => void search()}>搜索 Bangumi</Button></Space.Compact>
      {(searching || binding || refreshingCover) && <Typography.Text type="secondary">正在请求 Bangumi；请求较多时会排队等待，请勿重复提交。</Typography.Text>}
      {remoteError && <Alert type="warning" message={remoteError} />}
      {mergeSubject && <Button loading={mergeBusy} onClick={() => void prepareMerge()}>预览合并到已有番剧</Button>}
      {results?.localMatch && <Typography.Text>本地匹配：{results.localMatch.displayTitle}</Typography.Text>}
      {results && <List dataSource={results.items} locale={{ emptyText: '没有候选' }} renderItem={(item) => <List.Item actions={[<Button key="bind" onClick={() => { setMergeSubject(null); setMergePreview(null); setCandidate(item) }}>绑定 {item.nameCn || item.name}</Button>]}><Space direction="vertical"><Typography.Text>{item.nameCn || item.name}</Typography.Text><Typography.Text type="secondary">{item.date || '年份未知'} · {item.episodeCount} 话 · 评分 {item.score} · {detail.season || '季数未标注'}</Typography.Text></Space></List.Item>} />}
      <Typography.Title level={4}>近期记录</Typography.Title>
      <List dataSource={audits} locale={{ emptyText: '暂无记录' }} renderItem={(item) => <List.Item>{item.createdAt} · {item.action}</List.Item>} />
    </>}
    <Modal title="确认绑定 Bangumi" open={!!candidate} okText="确认绑定" okButtonProps={{ 'aria-label': '确认绑定', loading: binding }} onOk={() => void confirmBinding()} onCancel={() => setCandidate(null)}><p>将本地番剧 #{animeId} 绑定到 Bangumi #{candidate?.id}（{candidate?.nameCn || candidate?.name}）？</p></Modal>
    <Modal title="确认合并番剧" open={!!mergePreview} okText="确认合并" cancelText="取消" confirmLoading={mergeBusy} closable={!mergeBusy} maskClosable={!mergeBusy} cancelButtonProps={{ disabled: mergeBusy }} onOk={() => void confirmMerge()} onCancel={() => { if (!mergeBusy) setMergePreview(null) }}>
      {mergePreview && <><p>将「{mergePreview.sourceTitle}」合并到「{mergePreview.targetTitle}」（#{mergePreview.targetId}）。</p><p>转移 {mergePreview.mediaCount} 个媒体文件记录及 {mergePreview.aliasCount} 个别名；同集文件 {mergePreview.duplicateEpisodeCount} 个，全部保留。</p><p>保留已有番剧的资料和人工集数，自动集数按目标番剧重新映射。当前条目将移除，待执行整理计划将失效。实际文件及路径保持不变。</p><p>预览有效期为 5 分钟。</p></>}
    </Modal>
    <Modal title="修改番剧信息" open={!!edit} okText="保存信息" cancelText="取消" confirmLoading={saving} okButtonProps={{ 'aria-label': '保存信息', disabled: !edit?.displayTitle.trim() }} cancelButtonProps={{ disabled: saving }} closable={!saving} maskClosable={!saving} onOk={() => void saveEdit()} onCancel={() => { if (!saving) setEdit(null) }}>
      {edit && <Space direction="vertical" style={{ width: '100%' }}>
        {editError && <Alert type="error" message={editError} />}
        <label>番剧标题<Input aria-label="番剧标题" value={edit.displayTitle} maxLength={200} onChange={(event) => setEdit({ ...edit, displayTitle: event.target.value })} /></label>
        <label>季数<Input aria-label="番剧季数" value={edit.season} maxLength={40} onChange={(event) => setEdit({ ...edit, season: event.target.value })} /></label>
        <label>封面地址<Input aria-label="封面地址" value={edit.coverUrl} maxLength={2048} onChange={(event) => setEdit({ ...edit, coverUrl: event.target.value })} /></label>
      </Space>}
    </Modal>
    <Modal title="修改媒体集数" open={!!episodeEdit} okText="保存集数" cancelText="取消" confirmLoading={saving} okButtonProps={{ 'aria-label': '保存集数', disabled: !episodeEdit?.episodeNumber.trim() }} cancelButtonProps={{ disabled: saving }} closable={!saving} maskClosable={!saving} onOk={() => void saveEdit()} onCancel={() => { if (!saving) setEpisodeEdit(null) }}>
      {episodeEdit && <Space direction="vertical">
        {editError && <Alert type="error" message={editError} />}
        <label>媒体集数<Input aria-label="媒体集数" value={episodeEdit.episodeNumber} onChange={(event) => setEpisodeEdit({ ...episodeEdit, episodeNumber: event.target.value })} /></label>
        <label>集数类型<select aria-label="集数类型" value={episodeEdit.episodeType} onChange={(event) => setEpisodeEdit({ ...episodeEdit, episodeType: event.target.value })}>{['normal', 'sp', 'ova', 'ncop', 'nced'].map((type) => <option key={type} value={type}>{type}</option>)}</select></label>
      </Space>}
    </Modal>
  </Space>
}
