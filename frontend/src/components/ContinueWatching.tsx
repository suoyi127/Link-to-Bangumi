import { useState } from 'react'
import { Alert, Button, Card, List, Space, Typography } from 'antd'
import { playMedia } from '../api/media'
import { getContinueEpisodes } from '../api/watch'
import { useWatchSnapshot, watchTime } from './useWatchSnapshot'

export function ContinueWatching({ onOpenAnime }: { onOpenAnime: (id: number) => void }) {
  const { data, error } = useWatchSnapshot(getContinueEpisodes)
  const [playing, setPlaying] = useState<number | null>(null)
  const [notice, setNotice] = useState('')
  const [playError, setPlayError] = useState('')
  async function play(mediaId: number, label: string, nextEpisode: boolean) {
    setPlaying(mediaId); setNotice(''); setPlayError('')
    try {
      const result = nextEpisode ? await playMedia(mediaId) : await playMedia(mediaId, 'mpv')
      setNotice(`已启动${result.playerName || '播放器'}：${label}`)
    } catch (cause) { setPlayError(cause instanceof Error ? cause.message : '播放失败') }
    finally { setPlaying(null) }
  }
  if (!error && !data?.items?.length) return null
  return <section aria-label="继续观看">
    <Typography.Title level={4}>继续观看</Typography.Title>
    {error && <Alert type="warning" message={`继续观看读取失败：${error}`} />}
    {playError && <Alert type="error" message={playError} />}
    {notice && <Alert type="success" message={notice} />}
    <List grid={{ gutter: 12, xs: 1, sm: 2, lg: 3 }} dataSource={data?.items || []} locale={{ emptyText: '暂无观看记录' }} renderItem={item => {
      const episode = item.episodeType === 'normal' ? `第 ${item.episodeNumber} 集` : `${item.episodeType.toUpperCase()} ${item.episodeNumber}`
      const action = item.nextEpisode ? '播放下一集' : '用 mpv 继续播放'
      const label = `${item.title} ${episode}`
      return <List.Item><Card size="small"><Space direction="vertical">
        <Button type="link" onClick={() => onOpenAnime(item.animeId)}>{item.title}</Button>
        <Typography.Text>{episode}{!item.nextEpisode && ` · ${watchTime(item.position)}${item.duration > 0 ? ` / ${watchTime(item.duration)}` : ''}`}</Typography.Text>
        <Button aria-label={`${action} ${label}`} loading={playing === item.mediaId} disabled={playing !== null} onClick={() => void play(item.mediaId, label, item.nextEpisode)}>{action}</Button>
      </Space></Card></List.Item>
    }} />
  </section>
}
