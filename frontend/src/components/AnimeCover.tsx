import { useState } from 'react'
import type { Anime } from '../api/types'

export function AnimeCover({ item }: { item: Anime }) {
  const [failed, setFailed] = useState(false)
  // 图片脱离网格尺寸计算，避免原图宽高撑开全屏下的卡片与封面框。
  return <div className="anime-cover" style={{ position: 'relative', minWidth: 0, width: '100%', aspectRatio: '2 / 3', background: '#edf2f4', display: 'grid', placeItems: 'center', color: '#83959e', overflow: 'hidden' }}>
    {item.coverUrl && !failed ? <img src={item.coverUrl} alt={`${item.displayTitle} 封面`} style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', objectFit: 'cover' }} onError={() => setFailed(true)} /> : <span>暂无封面</span>}
  </div>
}
