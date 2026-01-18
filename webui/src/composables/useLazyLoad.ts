import { ref, onMounted, onUnmounted, type Ref } from 'vue'

interface LazyLoadOptions {
  rootMargin?: string
  threshold?: number | number[]
}

export function useLazyLoad(
  elementRef: Ref<HTMLElement | null>,
  onIntersect: (entry: IntersectionObserverEntry) => void,
  options: LazyLoadOptions = {}
) {
  const { rootMargin = '50px', threshold = 0.01 } = options
  const isIntersecting = ref(false)
  let observer: IntersectionObserver | null = null

  const observe = () => {
    if (!elementRef.value) return

    observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          isIntersecting.value = entry.isIntersecting
          if (entry.isIntersecting) {
            onIntersect(entry)
          }
        })
      },
      {
        rootMargin,
        threshold
      }
    )

    observer.observe(elementRef.value)
  }

  const unobserve = () => {
    if (observer && elementRef.value) {
      observer.unobserve(elementRef.value)
    }
  }

  const disconnect = () => {
    if (observer) {
      observer.disconnect()
      observer = null
    }
  }

  onMounted(() => {
    observe()
  })

  onUnmounted(() => {
    disconnect()
  })

  return {
    isIntersecting,
    observe,
    unobserve,
    disconnect
  }
}
