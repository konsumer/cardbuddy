// cardbuddy's page: tabs, and the install button's firmware (fw/, put there by tools/site.sh)

const $ = (s) => document.querySelector(s)

function tab(name) {
  for (const a of document.querySelectorAll('.tabs a')) a.classList.toggle('on', a.dataset.tab === name)
  for (const p of document.querySelectorAll('[data-page]')) p.hidden = p.dataset.page !== name
}
for (const a of document.querySelectorAll('[data-tab]')) {
  a.addEventListener('click', () => tab(a.dataset.tab))
}
tab(location.hash.slice(1) || 'install')

$('#install').setAttribute('manifest', 'fw/manifest.json')
fetch('fw/manifest.json')
  .then((r) => (r.ok ? r.json() : Promise.reject(new Error('none'))))
  .then((m) => {
    $('#release').textContent = `Firmware ${m.version}`
    $('#install').hidden = false
  })
  .catch(() => {
    $('#nofw').hidden = false
  })
