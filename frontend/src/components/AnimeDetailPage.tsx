import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Descriptions, Input, List, Modal, Space, Spin, Typography } from 'antd'
import { bindBangumi, getAnimeDetail, getAuditLogs, playMedia, refreshAnimeCover, searchBangumi } from '../api/client'
import type { AnimeDetail, AuditLog, BangumiCandidate, BangumiSearch, Media } from '../api/types'

type Props = { animeId: number; onBack: () => void }
const errorText = (error: unknown) => error instanceof Error ? error.message : '请求失败'
const mediaPageSize = 200

export function AnimeDetailPage({ animeId, onBack }: Props) {
  const [detail, setDetail] = useState<AnimeDetail | null>(null)
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
  const [refreshingCover, setRefreshingCover] = useState(false)
  const [coverError, setCoverError] = useState('')
  const [playingId, setPlayingId] = useState<number | null>(null)
  const [playError, setPlayError] = useState('')
  const [playNotice, setPlayNotice] = useState('')
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
  async function search() {
    const value = query.trim()
    if (new TextEncoder().encode(value).length < 1 || new TextEncoder().encode(value).length > 100) { setRemoteError('Bangumi 关键词须为 1–100 个 UTF-8 字节'); return }
    const current = ++searchGeneration.current
    setRemoteError(''); setResults(null)
    try { const found = await searchBangumi(value); if (current === searchGeneration.current) setResults(found) }
    catch (cause) { if (current === searchGeneration.current) setRemoteError(errorText(cause)) }
  }
  async function confirmBinding() {
    if (!candidate || binding) return
    const subjectId = candidate.id
    const current = generation.current
    setBinding(true); setRemoteError('')
    try {
      await bindBangumi(animeId, subjectId)
      if (current !== generation.current) return
      mediaGeneration.current++
      setMoreBusy(false)
      setCandidate(null); setResults(null)
      await reload(animeId, current)
    } catch (cause) { if (current === generation.current) setRemoteError(errorText(cause)) }
    finally { if (current === generation.current) setBinding(false) }
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
      await playMedia(item.id)
      if (current === generation.current) setPlayNotice(`已启动本机 mpv：${item.filename}`)
    } catch (cause) {
      if (current === generation.current) setPlayError(errorText(cause))
    } finally {
      if (current === generation.current) setPlayingId(null)
    }
  }

  return <Space direction="vertical" size="middle" style={{ width: '100%' }}>
    <Button onClick={onBack}>返回番剧库</Button>
    {error && <Alert type="error" message={error} />}
    {loading && <Spin aria-label="正在加载详情" />}
    {detail && <>
      <Typography.Title level={3}>{detail.displayTitle}</Typography.Title>
      {detail.coverUrl && <img src={detail.coverUrl} alt={`${detail.displayTitle} 封面`} style={{ width: 180, maxWidth: '100%', aspectRatio: '2 / 3', objectFit: 'cover' }} />}
      {detail.bangumiSubjectId && <Button loading={refreshingCover} onClick={() => void refreshCover()}>刮削封面</Button>}
      {coverError && <Alert type="warning" message={coverError} />}
      <Descriptions bordered column={1} items={[
        { key: 'season', label: '季', children: detail.season || '未标注' },
        { key: 'bangumi', label: 'Bangumi', children: detail.bangumiSubjectId ? <a href={`https://bgm.tv/subject/${detail.bangumiSubjectId}`} target="_blank" rel="noopener noreferrer">查看 Bangumi #{detail.bangumiSubjectId}</a> : <a href={`https://bgm.tv/subject_search/${encodeURIComponent(detail.displayTitle)}?cat=2`} target="_blank" rel="noopener noreferrer">在 Bangumi 搜索{detail.displayTitle}</a> },
        { key: 'aliases', label: '别名', children: detail.aliases.length ? detail.aliases.join('、') : '无' },
      ]} />
      <Typography.Title level={4}>媒体文件</Typography.Title>
      {playError && <Alert type="error" message={playError} />}
      {playNotice && <Alert type="success" message={playNotice} />}
      <List dataSource={media} locale={{ emptyText: '暂无媒体文件' }} renderItem={(item) => <List.Item key={item.id} actions={[<Button key="play" aria-label={`用 mpv 播放 ${item.filename}`} loading={playingId === item.id} disabled={playingId !== null && playingId !== item.id} onClick={() => void startPlayback(item)}>用 mpv 播放</Button>]}><Space direction="vertical"><Typography.Text>{item.filename}</Typography.Text><Typography.Text type="secondary">{item.episodeType} {item.episodeNumber} · {item.status} · {item.origin}</Typography.Text><Typography.Text copyable>{item.sourcePath}</Typography.Text></Space></List.Item>} />
      {nextMediaOffset !== null && <Button loading={moreBusy} onClick={() => void loadMore()}>加载更多媒体</Button>}
      <Typography.Title level={4}>Bangumi 绑定</Typography.Title>
      <Space.Compact><Input aria-label="Bangumi 关键词" value={query} onChange={(event) => setQuery(event.target.value)} /><Button onClick={() => void search()}>搜索 Bangumi</Button></Space.Compact>
      {remoteError && <Alert type="warning" message={remoteError} />}
      {results?.localMatch && <Typography.Text>本地匹配：{results.localMatch.displayTitle}</Typography.Text>}
      {results && <List dataSource={results.items} locale={{ emptyText: '没有候选' }} renderItem={(item) => <List.Item actions={[<Button key="bind" onClick={() => setCandidate(item)}>绑定 {item.nameCn || item.name}</Button>]}><Space direction="vertical"><Typography.Text>{item.nameCn || item.name}</Typography.Text><Typography.Text type="secondary">{item.date || '年份未知'} · {item.episodeCount} 话 · 评分 {item.score} · {detail.season || '季数未标注'}</Typography.Text></Space></List.Item>} />}
      <Typography.Title level={4}>近期记录</Typography.Title>
      <List dataSource={audits} locale={{ emptyText: '暂无记录' }} renderItem={(item) => <List.Item>{item.createdAt} · {item.action}</List.Item>} />
    </>}
    <Modal title="确认绑定 Bangumi" open={!!candidate} okText="确认绑定" okButtonProps={{ 'aria-label': '确认绑定', loading: binding }} onOk={() => void confirmBinding()} onCancel={() => setCandidate(null)}><p>将本地番剧 #{animeId} 绑定到 Bangumi #{candidate?.id}（{candidate?.nameCn || candidate?.name}）？</p></Modal>
  </Space>
}
