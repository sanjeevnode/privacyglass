// WhatsApp Web DOM selectors -- THE ONLY FILE THAT SHOULD NEED EDITING WHEN
// WHATSAPP CHANGES ITS DOM.
//
// WhatsApp ships obfuscated, generated class names (`.x1n2onr6`, `._ak8j`) that
// churn without notice. Everything here prefers, in order:
//   1. ARIA roles / semantic attributes  -- WhatsApp needs these for a11y, so they
//      are the most stable thing on the page.
//   2. data-* attributes                 -- semi-stable, survive most restyles.
//   3. Generated class names              -- last resort, expect breakage.
//
// STRATEGY: enumerating every text node is a losing game -- one missed span is a
// leaked phone number. Instead each category blurs a CONTAINER and `exempt`
// lists the UI chrome that must stay readable inside it. Missing a selector then
// over-blurs (safe) instead of leaking (not safe).

window.__wapSelectors = {
  // Contact/group display names: chat list rows and the open chat's header.
  names: [
    '#pane-side [role="listitem"] span[title]',
    '#pane-side [role="gridcell"] span[title]',
    '#main header span[title]',
    '#main header [role="button"] span[dir="auto"]',
    // Group-message sender labels ("+91 91710 89361" above a bubble). These are
    // colour-coded spans inside the bubble, NOT covered by the header rules.
    '#main [data-pre-plain-text]',
    '#main .message-in span[aria-label]',
    '#main [role="row"] span[dir="auto"][class*="_ak"]',
    // Contact-info / drawer panel name.
    '[data-testid="drawer-right"] span[title]',
    'span[title][dir="auto"]',
  ],

  // Message bubble text in the open conversation.
  messages: [
    '#main .message-in .selectable-text',
    '#main .message-out .selectable-text',
    '#main div.copyable-text span.selectable-text',
    '#main [data-testid="quoted-message"]',
    // Whole bubble body, so captions/link previews inside it go too.
    '#main .message-in .copyable-text',
    '#main .message-out .copyable-text',
  ],

  // Avatars / profile photos, list and header alike.
  pictures: [
    'img[src^="blob:"]',
    'img[src*="/pps/"]',
    'img[src*="mmg.whatsapp.net"]',
    'img[src*="pps.whatsapp.net"]',
    '#pane-side [role="listitem"] img',
    '#main header img',
    // Fallback silhouettes rendered as inline SVG when there is no photo --
    // these are what leaked as coloured circles in testing.
    '#pane-side [role="listitem"] [data-icon="default-user"]',
    '#pane-side [role="listitem"] [data-icon="default-group"]',
    '#main header [data-icon="default-user"]',
    '#main header [data-icon="default-group"]',
    '[data-testid="default-user"]',
    '[data-testid="default-group"]',
    '[data-testid="avatar"]',
  ],

  // Chat-list secondary line (last-message preview) and in-chat media.
  previews: [
    // The preview text row only -- NOT the whole gridcell, because an ancestor's
    // blur also rasterizes the timestamp and unread badge and cannot be undone
    // on the child.
    // Innermost text spans only. A bare span[dir="auto"] also matches the row
    // wrapper, and blurring that ancestor drags the avatar down with it -- which
    // makes the Photos checkbox look broken.
    '#pane-side [role="listitem"] [role="gridcell"] span[dir="ltr"]',
    '#pane-side [role="listitem"] span[dir="ltr"]:not(:has(img))',
    '#pane-side [role="listitem"] span[dir="auto"]:not([title]):not(:has(img))',
    '#main [data-testid="media-content"]',
    '#main .message-in img, #main .message-out img',
  ],
};

// Elements that must NEVER be blurred, even when inside a blurred container.
// Without these the container strategy above would blur the timestamps and
// unread badges that make the list usable.
window.__wapExempt = [
  '#pane-side [role="listitem"] [role="gridcell"] > div:last-child',  // time + badge column
  '[data-icon="muted"]',
  '[data-icon="pinned"]',
  '[data-icon="status-dblcheck"]',
  '[data-icon="status-check"]',
  '[data-icon="status-time"]',
  '[aria-label*="unread"]',
];
