import { useEffect, useState } from 'react'
import { Alert, Button, Card, Input, List, Space, Spin, Tag, Typography } from 'antd'
import { getAnimeCalendar } from '../api/client'
import type { AnimeCalendar } from '../api/types'
import { AnimeCover } from './AnimeCover'

const weekdays = ['星期一', '星期二', '星期三', '星期四', '星期五', '星期六', '星期日']

export function LibraryCalendar({ onOpenAnime }: { onOpenAnime: (id: number) => void }) {
  const [calendar, setCalendar] = useState<AnimeCalendar | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [retry, setRetry] = useState(0)
  const [filter, setFilter] = useState('')
  useEffect(() => {
    const controller = new AbortController()
    setLoading(true); setError('')
    void getAnimeCalendar(controller.signal).then((result) => {
      if (!controller.signal.aborted) setCalendar(result)
    }).catch((cause) => {
      if (!controller.signal.aborted) setError(cause instanceof Error ? cause.message : '排期加载失败')
    }).finally(() => { if (!controller.signal.aborted) setLoading(false) })
    return () => controller.abort()
  }, [retry])

  const today = new Intl.DateTimeFormat('zh-CN', { weekday: 'long', timeZone: 'Asia/Shanghai' }).format(new Date())
  const keyword = filter.trim().toLocaleLowerCase()
  const total = calendar?.days.reduce((count, day) => count + day.items.length, 0) ?? 0
  return <Space direction="vertical" size="large" style={{ width: '100%' }}>
    <Space wrap>
      <Input.Search aria-label="筛选排期番剧" placeholder="筛选排期番剧" value={filter} onChange={(event) => setFilter(event.target.value)} allowClear style={{ width: 240 }} />
      <Button disabled={loading} onClick={() => setRetry((value) => value + 1)}>{error ? '重试' : '刷新'}</Button>
    </Space>
    {error && <Alert type="error" showIcon message={error} description="请检查设置中的 Bangumi 连接，稍后重试。" />}
    {loading ? <Spin aria-label="正在加载每周排期" /> : calendar && <>
      {calendar.stale && <Alert type="warning" showIcon message="Bangumi 暂时不可用，正在显示最近缓存的排期。" />}
      <Typography.Text type="secondary">排期中有 {total} 部本地番剧 · 数据每六小时更新{calendar.updatedAt > 0 ? ` · 最近更新 ${new Date(calendar.updatedAt * 1000).toLocaleString('zh-CN')}` : ''}</Typography.Text>
      {total === 0 && <Alert type="info" showIcon message="暂无符合排期的本地番剧" description="仅显示 Bangumi 每日放送表中、已绑定 Bangumi 且有本地媒体文件的番剧。" />}
      <div className="calendar-week-grid">{calendar.days.map((day) => {
        const label = weekdays[day.weekday - 1]
        const items = day.items.filter((item) => `${item.displayTitle} ${item.originalTitle}`.toLocaleLowerCase().includes(keyword))
        return <section className="calendar-day" key={day.weekday} aria-label={`${label}排期`} style={{ width: '100%' }}>
          <Typography.Title level={4}>{label} {label === today && <Tag color="blue">今天</Tag>} <Typography.Text type="secondary">{items.length} 部</Typography.Text></Typography.Title>
          {items.length === 0 ? <Typography.Text type="secondary">{keyword ? '没有匹配的番剧' : '暂无本地番剧'}</Typography.Text> : <List grid={{ gutter: 16, column: 2 }} dataSource={items} renderItem={(item) => <List.Item>
            <Card hoverable cover={<AnimeCover key={`${item.id}-${item.coverUrl}`} item={item} />} styles={{ body: { padding: 12 } }} style={{ overflow: 'hidden' }}>
              <Button type="link" aria-label={`打开 ${item.displayTitle}`} onClick={() => onOpenAnime(item.id)} style={{ maxWidth: '100%', overflow: 'hidden', textOverflow: 'ellipsis' }}>{item.displayTitle}</Button>
              <div><Typography.Text type="secondary">{item.season || '季数未标注'}</Typography.Text></div>
            </Card>
          </List.Item>} />}
        </section>
      })}</div>
    </>}
  </Space>
}
