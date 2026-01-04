<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue'
import { useAssistantStore } from '../stores/assistant'

const store = useAssistantStore()

// Custom answer input
const customAnswer = ref('')
const customInputRef = ref<HTMLInputElement | null>(null)

// Computed
const hasQuestion = computed(() => store.pendingQuestion !== null)
const isMinimized = computed(() => store.questionMinimized)

// Focus input when question appears
watch(() => store.pendingQuestion, (question) => {
  if (question) {
    customAnswer.value = ''
    nextTick(() => {
      customInputRef.value?.focus()
    })
  }
})

// Handle selecting a predefined option
function selectOption(option: string) {
  store.answerQuestion(option)
}

// Handle submitting custom answer
function submitCustomAnswer() {
  if (customAnswer.value.trim()) {
    store.answerQuestion(customAnswer.value.trim())
    customAnswer.value = ''
  }
}

// Handle key press in custom input
function handleKeyPress(e: KeyboardEvent) {
  if (e.key === 'Enter' && customAnswer.value.trim()) {
    submitCustomAnswer()
  }
}

// Minimize/restore the modal
function toggleMinimize() {
  store.toggleQuestionMinimized()
}

// Dismiss the question
function dismiss() {
  store.dismissQuestion()
}
</script>

<template>
  <!-- Minimized indicator - always visible when minimized -->
  <Transition name="slide-up">
    <div v-if="hasQuestion && isMinimized" class="question-minimized" @click="toggleMinimize">
      <span class="minimized-icon">?</span>
      <span class="minimized-text">{{ store.pendingQuestion?.title || 'Question' }}</span>
      <span class="minimized-badge">Waiting for answer</span>
    </div>
  </Transition>

  <!-- Full modal -->
  <Transition name="modal">
    <div v-if="hasQuestion && !isMinimized" class="question-overlay">
      <div class="question-modal">
        <!-- Header -->
        <div class="question-header">
          <div class="header-left">
            <span class="header-icon">?</span>
            <h3 class="header-title">{{ store.pendingQuestion?.title }}</h3>
          </div>
          <div class="header-actions">
            <button class="header-btn" @click="toggleMinimize" title="Minimize">
              <span class="minimize-icon">_</span>
            </button>
            <button class="header-btn close-btn" @click="dismiss" title="Dismiss">
              <span class="close-icon">&times;</span>
            </button>
          </div>
        </div>

        <!-- Question content -->
        <div class="question-content">
          <p class="question-text">{{ store.pendingQuestion?.question }}</p>

          <!-- Predefined options -->
          <div v-if="store.pendingQuestion?.options?.length" class="question-options">
            <button
              v-for="option in store.pendingQuestion.options"
              :key="option"
              class="option-btn"
              @click="selectOption(option)"
            >
              {{ option }}
            </button>
          </div>

          <!-- Custom answer input - always available -->
          <div class="custom-answer">
            <div class="custom-divider" v-if="store.pendingQuestion?.options?.length">
              <span>or type your own answer</span>
            </div>
            <div class="custom-input-wrapper">
              <input
                ref="customInputRef"
                v-model="customAnswer"
                type="text"
                class="custom-input"
                placeholder="Type your answer..."
                @keypress="handleKeyPress"
              />
              <button
                class="submit-btn"
                :disabled="!customAnswer.trim()"
                @click="submitCustomAnswer"
              >
                Send
              </button>
            </div>
          </div>
        </div>

        <!-- Footer -->
        <div class="question-footer">
          <span class="footer-note">The assistant is waiting for your response</span>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
/* Minimized indicator */
.question-minimized {
  position: fixed;
  bottom: 80px;
  right: 24px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  background: linear-gradient(135deg, var(--accent-primary) 0%, var(--accent-secondary) 100%);
  border-radius: var(--border-radius);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
  cursor: pointer;
  z-index: 1000;
  transition: transform 0.2s, box-shadow 0.2s;
}

.question-minimized:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 24px rgba(0, 0, 0, 0.4);
}

.minimized-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  background: rgba(255, 255, 255, 0.2);
  border-radius: 50%;
  font-weight: bold;
  color: white;
}

.minimized-text {
  font-weight: 500;
  color: white;
}

.minimized-badge {
  padding: 2px 8px;
  background: rgba(255, 255, 255, 0.2);
  border-radius: var(--border-radius-sm);
  font-size: 11px;
  color: white;
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 0.8; }
  50% { opacity: 1; }
}

/* Modal overlay */
.question-overlay {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(4px);
  z-index: 1001;
}

/* Modal container */
.question-modal {
  width: 90%;
  max-width: 500px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-lg);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  overflow: hidden;
}

/* Header */
.question-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 20px;
  background: linear-gradient(135deg, var(--accent-primary) 0%, var(--accent-secondary) 100%);
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.header-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  background: rgba(255, 255, 255, 0.2);
  border-radius: 50%;
  font-size: 18px;
  font-weight: bold;
  color: white;
}

.header-title {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: white;
}

.header-actions {
  display: flex;
  gap: 8px;
}

.header-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  background: rgba(255, 255, 255, 0.15);
  border: none;
  border-radius: var(--border-radius-sm);
  color: white;
  cursor: pointer;
  transition: background 0.2s;
}

.header-btn:hover {
  background: rgba(255, 255, 255, 0.3);
}

.minimize-icon {
  font-size: 18px;
  font-weight: bold;
  line-height: 1;
}

.close-icon {
  font-size: 20px;
  line-height: 1;
}

/* Content */
.question-content {
  padding: 24px 20px;
}

.question-text {
  margin: 0 0 20px;
  font-size: 15px;
  line-height: 1.6;
  color: var(--text-primary);
}

/* Options */
.question-options {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.option-btn {
  padding: 12px 16px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  color: var(--text-primary);
  font-size: 14px;
  text-align: left;
  cursor: pointer;
  transition: all 0.2s;
}

.option-btn:hover {
  background: var(--accent-primary);
  border-color: var(--accent-primary);
  color: var(--bg-primary);
  transform: translateX(4px);
}

/* Custom answer */
.custom-answer {
  margin-top: 4px;
}

.question-options + .custom-answer {
  margin-top: 16px;
}

.custom-divider {
  display: flex;
  align-items: center;
  margin-bottom: 16px;
  color: var(--text-muted);
  font-size: 12px;
}

.custom-divider::before,
.custom-divider::after {
  content: '';
  flex: 1;
  height: 1px;
  background: var(--border-color);
}

.custom-divider span {
  padding: 0 12px;
}

.custom-input-wrapper {
  display: flex;
  gap: 8px;
}

.custom-input {
  flex: 1;
  padding: 10px 14px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  color: var(--text-primary);
  font-size: 14px;
}

.custom-input:focus {
  outline: none;
  border-color: var(--accent-primary);
}

.submit-btn {
  padding: 10px 20px;
  background: var(--accent-primary);
  border: none;
  border-radius: var(--border-radius);
  color: var(--bg-primary);
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.2s, opacity 0.2s;
}

.submit-btn:hover:not(:disabled) {
  background: var(--accent-secondary);
}

.submit-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* Footer */
.question-footer {
  padding: 12px 20px;
  background: var(--bg-tertiary);
  border-top: 1px solid var(--border-color);
}

.footer-note {
  font-size: 12px;
  color: var(--text-muted);
}

/* Transitions */
.modal-enter-active,
.modal-leave-active {
  transition: opacity 0.2s ease;
}

.modal-enter-active .question-modal,
.modal-leave-active .question-modal {
  transition: transform 0.2s ease;
}

.modal-enter-from,
.modal-leave-to {
  opacity: 0;
}

.modal-enter-from .question-modal {
  transform: scale(0.95) translateY(-20px);
}

.modal-leave-to .question-modal {
  transform: scale(0.95) translateY(20px);
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: all 0.3s ease;
}

.slide-up-enter-from,
.slide-up-leave-to {
  opacity: 0;
  transform: translateY(20px);
}
</style>
