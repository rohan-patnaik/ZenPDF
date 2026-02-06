# ZenPDF Practical UI Redesign Checklist

Goal: Keep the current white/green minimal style while making feature placement intentional, practical, and task-oriented.

## 1. Global Design System
- [x] Keep a restrained white/green visual language with subtle borders and shadows.
- [x] Standardize reusable primitives (`paper-card`, button variants, badges, alerts, form fields).
- [x] Ensure focus-visible states are clearly visible and consistent.
- [x] Keep typography hierarchy compact and readable (no decorative oversizing).

## 2. Navigation and Orientation
- [x] Keep primary navigation minimal: `Tools`, `Usage & Capacity`, auth.
- [x] Highlight active navigation item to reduce orientation ambiguity.
- [x] Keep utility actions (sign-in/user menu) visually secondary to task navigation.

## 3. Home Page IA
- [x] First viewport answers: what ZenPDF does + what to do next.
- [x] Keep primary CTA (`Start with a file`) and secondary CTA (`View usage limits`) adjacent.
- [x] Present tools in intent-based groups instead of flat lists.
- [x] Keep limits/workflow panels as support context, not primary action space.

## 4. Tools Page IA (Core Workflow)
- [x] Explicit top flow framing: Select tool -> Configure -> Queue/Monitor.
- [x] Place tool selection in left rail and group by user intent.
- [x] Keep active tool details and configuration in the central action panel.
- [x] Show required settings before optional settings.
- [x] Keep advanced options collapsed by default (progressive disclosure).
- [x] Keep submit CTA close to required inputs.
- [x] Keep active job progress and status near submit area.
- [x] Keep recent jobs below as historical context, not competing with primary task.

## 5. Usage & Capacity Page IA
- [x] Separate personal usage from shared pool usage.
- [x] Keep capacity state visible at top with clear status tone.
- [x] Keep access tier details in a dedicated side panel.
- [x] Present error catalog with disclosure to reduce cognitive load.

## 6. Feedback and State Design
- [x] Standardize success/warning/error states for consistency.
- [x] Ensure empty states include next-step guidance.
- [x] Keep loading states explicit and scoped to the section being loaded.

## 7. Accessibility and Responsiveness
- [x] Maintain keyboard-focus visibility across interactive controls.
- [x] Keep semantic structures (`nav`, headings, sections, details/summary).
- [x] Preserve readable spacing and action order on mobile layouts.

## 8. Safety Constraints
- [x] UI-only changes; no backend/API/auth/data-model behavior changes.
- [x] Preserve all existing features and functional outcomes.
