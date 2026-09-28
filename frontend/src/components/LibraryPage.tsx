import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Card, Input, List, Space, Spin, Table, Tag, Typography } from 'antd'
import { getAnime } from '../api/client'
import type { Anime } from '../api/types'

type Props = { onOpenAnime: (id: number) => void }
const pageSize = 200
const errorText = (error: unknown) => error instanceof Error ? error.message : '加载失败'
function Cover({ item }: { item: Anime }) {
  const [failed, setFailed] = useState(false)
  return <div style={{ width: '100%', aspectRatio: '2 / 3', background: '#202934', display: 'grid', placeItems: 'center', color: '#b9c2cc', overflow: 'hidden' }}>
    {item.coverUrl && !failed ? <img src={item.coverUrl} alt={`${item.displayTitle} 封面`} style={{ width: '100%', height: '100%', objectFit: 'cover' }} onError={() => setFailed(true)} /> : <span>暂无封面</span>}
  </div>
}

export function LibraryPage({ onOpenAnime }: Props) {
  // 番剧目录采用分页加载；文件列表、别名与操作入口放在详情页按需读取。
  const [view, setView] = useState<'grid' | 'list'>('grid')
  const [filter, setFilter] = useState('')
  const [items, setItems] = useState<Anime[]>([])
  const [offset, setOffset] = useState(0)
  const [nextOffset, setNextOffset] = useState<number | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const sequence = useRef(0)
  useEffect(() => {
    const controller = new AbortController()
    const sequenceRef = sequence
    const current = ++sequenceRef.current
    setLoading(true)
    setError('')
    void getAnime(pageSize, offset, controller.signal).then((page) => {
      if (current !== sequence.current) return
      setItems(page.items)
      setNextOffset(page.nextOffset)
    }).catch((cause) => { if (!controller.signal.aborted && current === sequence.current) setError(errorText(cause)) })
      .finally(() => { if (!controller.signal.aborted && current === sequence.current) setLoading(false) })
    return () => { controller.abort(); if (sequenceRef.current === current) sequenceRef.current++ }
  }, [offset])

  const title = (item: Anime) => <Button type="link" aria-label={`打开 ${item.displayTitle}`} onClick={() => onOpenAnime(item.id)}>{item.displayTitle}</Button>
  const coverCell = (item: Anime) => <Cover key={`${item.id}-${item.coverUrl}`} item={item} />
  const visibleItems = items.filter((item) => `${item.displayTitle} ${item.originalTitle}`.toLocaleLowerCase().includes(filter.trim().toLocaleLowerCase()))
  return <Space direction="vertical" size="large" style={{ width: '100%' }}>
    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 12 }}>
      <div><Typography.Title level={3} style={{ margin: 0 }}>媒体库</Typography.Title><Typography.Text type="secondary">按番剧浏览 · 当前页 {items.length} 部</Typography.Text></div>
      <Space wrap><Input.Search aria-label="筛选本页番剧" placeholder="筛选本页番剧" value={filter} onChange={(event) => setFilter(event.target.value)} style={{ width: 240 }} allowClear /><Button aria-label="网格视图" type={view === 'grid' ? 'primary' : 'default'} onClick={() => setView('grid')}>网格</Button><Button aria-label="列表视图" type={view === 'list' ? 'primary' : 'default'} onClick={() => setView('list')}>列表</Button></Space>
    </div>
    {error && <Alert type="error" showIcon message={error} />}
    {loading ? <Spin aria-label="正在加载番剧库" /> : items.length === 0 ? <Typography.Text type="secondary">暂无本地番剧</Typography.Text> : visibleItems.length === 0 ? <Typography.Text type="secondary">本页没有匹配的番剧</Typography.Text> : view === 'grid' ?
      <List grid={{ gutter: 20, xs: 2, sm: 3, md: 4, lg: 5, xl: 6 }} dataSource={visibleItems} renderItem={(item) => <List.Item><Card hoverable cover={coverCell(item)} styles={{ body: { padding: 12 } }} style={{ overflow: 'hidden' }}><div style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{title(item)}</div><Typography.Text type="secondary">{item.season || '季数未标注'}</Typography.Text><div><Tag color={item.bangumiSubjectId ? 'green' : 'default'}>{item.bangumiSubjectId ? '已绑定 Bangumi' : '未绑定 Bangumi'}</Tag></div></Card></List.Item>} /> :
      <Table rowKey="id" pagination={false} dataSource={visibleItems} columns={[{ title: '封面', width: 90, render: (_, item) => <div style={{ width: 60 }}>{coverCell(item)}</div> }, { title: '标题', render: (_, item) => title(item) }, { title: '季', dataIndex: 'season' }, { title: 'Bangumi', render: (_, item) => item.bangumiSubjectId ? '已绑定' : '未绑定' }]} />}
    <Space><Typography.Text>第 {Math.floor(offset / pageSize) + 1} 页</Typography.Text><Button disabled={loading || offset === 0} onClick={() => setOffset(Math.max(0, offset - pageSize))}>上一页</Button><Button disabled={loading || nextOffset === null} onClick={() => { if (nextOffset !== null) setOffset(nextOffset) }}>下一页</Button></Space>
  </Space>
}
