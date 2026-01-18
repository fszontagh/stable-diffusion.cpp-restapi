import { computed, type Ref } from 'vue'
import { useScroll, useElementSize } from '@vueuse/core'

interface VirtualScrollOptions {
  itemHeight: number
  buffer?: number
}

export function useVirtualScroll<T>(
  items: Ref<T[]>,
  containerRef: Ref<HTMLElement | null>,
  options: VirtualScrollOptions
) {
  const { itemHeight, buffer = 5 } = options

  const { y: scrollTop } = useScroll(containerRef, { behavior: 'smooth' })
  const { height: containerHeight } = useElementSize(containerRef)

  const visibleItems = computed(() => {
    if (!containerHeight.value || items.value.length === 0) {
      return {
        items: items.value,
        offsetY: 0,
        totalHeight: items.value.length * itemHeight
      }
    }

    const startIndex = Math.max(0, Math.floor(scrollTop.value / itemHeight) - buffer)
    const endIndex = Math.min(
      items.value.length,
      Math.ceil((scrollTop.value + containerHeight.value) / itemHeight) + buffer
    )

    const visibleSlice = items.value.slice(startIndex, endIndex)
    const offsetY = startIndex * itemHeight
    const totalHeight = items.value.length * itemHeight

    return {
      items: visibleSlice,
      offsetY,
      totalHeight,
      startIndex,
      endIndex
    }
  })

  return {
    visibleItems,
    scrollTop
  }
}
