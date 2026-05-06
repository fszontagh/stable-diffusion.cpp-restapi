// Combinatorial expansion of dynamic-prompt syntax.
//
// Mirrors src/prompt_template.cpp on the backend — this file exists for the
// frontend's count preview + preview-of-first-N expanded prompts in the
// confirmation modal. Semantics MUST stay identical: the warning lies if
// they diverge.
//
// Supported:
//   {a|b|c}            — alternation, expands to 3 prompts.
//   {N$$a|b|c|d}       — pick-N, expands to C(k, N) prompts (joined by ", ").
//   Nesting and escapes (\{ \} \| \\) supported.

type Atom = { kind: 'lit'; value: string } | { kind: 'group'; group: Group }
type Sequence = Atom[]
interface Group {
  options: Sequence[]
  pickN: number  // 1 = simple alternation
}

class Parser {
  private pos = 0
  constructor(private src: string) {}

  parseTop(): Sequence {
    const seq = this.parseSequence(false)
    if (this.pos !== this.src.length) {
      throw new Error(`Prompt template: unexpected '}' at position ${this.pos}`)
    }
    return seq
  }

  private parseSequence(stopOnBrace: boolean): Sequence {
    const out: Sequence = []
    let buf = ''

    const flush = () => {
      if (buf) {
        out.push({ kind: 'lit', value: buf })
        buf = ''
      }
    }

    while (this.pos < this.src.length) {
      const c = this.src[this.pos]

      if (stopOnBrace && (c === '|' || c === '}')) break

      if (c === '\\' && this.pos + 1 < this.src.length) {
        const next = this.src[this.pos + 1]
        if (next === '{' || next === '}' || next === '|' || next === '\\') {
          buf += next
          this.pos += 2
          continue
        }
      }

      if (c === '{') {
        flush()
        out.push({ kind: 'group', group: this.parseGroup() })
        continue
      }

      buf += c
      this.pos++
    }

    flush()
    return out
  }

  private parseGroup(): Group {
    this.pos++ // consume '{'
    const g: Group = { options: [], pickN: 1 }

    // Optional N$$ prefix.
    const save = this.pos
    let pick = 0
    let hasDigits = false
    while (this.pos < this.src.length && /\d/.test(this.src[this.pos])) {
      pick = pick * 10 + parseInt(this.src[this.pos], 10)
      this.pos++
      hasDigits = true
    }
    if (
      hasDigits &&
      this.pos + 1 < this.src.length &&
      this.src[this.pos] === '$' &&
      this.src[this.pos + 1] === '$'
    ) {
      this.pos += 2
      if (pick <= 0) throw new Error('Prompt template: pick-N count must be >= 1')
      g.pickN = pick
    } else {
      // Not pick-N — rewind so digits go into the first option's literal.
      this.pos = save
    }

    while (true) {
      g.options.push(this.parseSequence(true))
      if (this.pos >= this.src.length) {
        throw new Error("Prompt template: unterminated '{' (missing '}')")
      }
      const c = this.src[this.pos]
      if (c === '|') {
        this.pos++
        continue
      }
      if (c === '}') {
        this.pos++
        break
      }
      throw new Error(`Prompt template: parser desync at ${this.pos}`)
    }

    if (g.pickN > g.options.length) {
      throw new Error(
        `Prompt template: pick-N count ${g.pickN} exceeds number of options ${g.options.length}`
      )
    }
    return g
  }
}

function* combinations(k: number, n: number): Generator<number[]> {
  // Yield all C(k, n) index lists in lexicographic order.
  if (n > k || n <= 0) return
  const idx = Array.from({ length: n }, (_, i) => i)
  while (true) {
    yield idx.slice()
    let i = n - 1
    while (i >= 0 && idx[i] === k - n + i) i--
    if (i < 0) return
    idx[i]++
    for (let j = i + 1; j < n; j++) idx[j] = idx[j - 1] + 1
  }
}

function expandAtom(atom: Atom): string[] {
  if (atom.kind === 'lit') return [atom.value]
  const g = atom.group

  if (g.pickN === 1) {
    const out: string[] = []
    for (const opt of g.options) {
      for (const s of expandSequence(opt)) out.push(s)
    }
    return out
  }

  const perOption = g.options.map(expandSequence)
  const out: string[] = []
  for (const indices of combinations(g.options.length, g.pickN)) {
    let acc: string[] = ['']
    for (const idx of indices) {
      const choices = perOption[idx]
      const next: string[] = []
      for (const a of acc) {
        for (const c of choices) {
          next.push(a === '' ? c : `${a}, ${c}`)
        }
      }
      acc = next
    }
    for (const s of acc) out.push(s)
  }
  return out
}

function expandSequence(seq: Sequence): string[] {
  let acc: string[] = ['']
  for (const atom of seq) {
    const frags = expandAtom(atom)
    if (frags.length === 0) return []
    const next: string[] = []
    for (const a of acc) for (const f of frags) next.push(a + f)
    acc = next
  }
  return acc
}

function countAtom(atom: Atom): number {
  if (atom.kind === 'lit') return 1
  const g = atom.group
  if (g.pickN === 1) {
    let total = 0
    for (const opt of g.options) total += countSequence(opt)
    return total
  }
  const perOption = g.options.map(countSequence)
  let total = 0
  for (const indices of combinations(g.options.length, g.pickN)) {
    let product = 1
    for (const idx of indices) product *= perOption[idx]
    total += product
  }
  return total
}

function countSequence(seq: Sequence): number {
  let product = 1
  for (const atom of seq) product *= countAtom(atom)
  return product
}

function dedupStable(v: string[]): string[] {
  const seen = new Set<string>()
  const out: string[] = []
  for (const s of v) {
    if (!seen.has(s)) {
      seen.add(s)
      out.push(s)
    }
  }
  return out
}

/**
 * Expand a templated prompt into all unique variations.
 * Returns the input unchanged (as a single-element array) if there is no
 * template syntax. Throws on malformed input — callers should wrap in
 * try/catch and surface a friendly error.
 */
export function expandPrompt(templated: string): string[] {
  if (!templated.includes('{')) return [templated]
  const seq = new Parser(templated).parseTop()
  return dedupStable(expandSequence(seq))
}

/**
 * Count variations without materializing strings. Cheaper for large fan-outs.
 * Note: this is the upper bound — duplicates produced by overlapping branches
 * (e.g. {a|a|b}) are NOT removed in the count, only in the actual expansion.
 * The confirmation modal should display this number prefixed with "up to" if
 * the user might care, or just use expandPrompt(prompt).length for the exact
 * job count.
 */
export function countVariations(templated: string): number {
  if (!templated.includes('{')) return 1
  const seq = new Parser(templated).parseTop()
  return countSequence(seq)
}

/**
 * True iff the prompt contains any template syntax (i.e. expansion would
 * produce more than 1 variation, or be malformed). Useful for showing the
 * "expand prompt" affordance only when relevant.
 */
export function hasTemplateSyntax(templated: string): boolean {
  // Any unescaped '{' is enough — the parser catches malformed cases later.
  for (let i = 0; i < templated.length; i++) {
    if (templated[i] === '\\' && i + 1 < templated.length) {
      i++ // skip escaped char
      continue
    }
    if (templated[i] === '{') return true
  }
  return false
}
