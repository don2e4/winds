document.addEventListener('DOMContentLoaded', () => {
  // Mobile Sidebar Toggle
  const menuBtn = document.getElementById('mobileMenuBtn');
  const sidebar = document.getElementById('sidebar');

  if (menuBtn && sidebar) {
    menuBtn.addEventListener('click', () => {
      sidebar.classList.toggle('open');
    });

    // Close on navigation click
    sidebar.querySelectorAll('a').forEach(link => {
      link.addEventListener('click', () => {
        sidebar.classList.remove('open');
      });
    });
  }

  // Active Link Tracking
  const sections = document.querySelectorAll('.doc-section');
  const navLinks = document.querySelectorAll('.sidebar-links a');

  const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        const id = entry.target.getAttribute('id');
        navLinks.forEach(link => {
          if (link.getAttribute('href') === `#${id}`) {
            link.classList.add('active');
          } else {
            link.classList.remove('active');
          }
        });
      }
    });
  }, { rootMargin: '-20% 0px -70% 0px' });

  sections.forEach(sec => observer.observe(sec));

  // FAQ Accordion
  const faqItems = document.querySelectorAll('.faq-item');
  faqItems.forEach(item => {
    const question = item.querySelector('.faq-question');
    question.addEventListener('click', () => {
      const isActive = item.classList.contains('active');
      faqItems.forEach(i => i.classList.remove('active'));
      if (!isActive) {
        item.classList.add('active');
      }
    });
  });

  // Code Tab Switching
  const tabContainers = document.querySelectorAll('.code-container');
  tabContainers.forEach(container => {
    const tabs = container.querySelectorAll('.code-tab');
    const blocks = container.querySelectorAll('pre');

    tabs.forEach((tab, index) => {
      tab.addEventListener('click', () => {
        tabs.forEach(t => t.classList.remove('active'));
        blocks.forEach(b => b.style.display = 'none');

        tab.classList.add('active');
        if (blocks[index]) {
          blocks[index].style.display = 'block';
        }
      });
    });
  });

  // Copy Buttons
  const copyButtons = document.querySelectorAll('.copy-btn');
  copyButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      const targetSelector = btn.getAttribute('data-target');
      let textToCopy = '';
      if (targetSelector) {
        const targetEl = document.querySelector(targetSelector);
        if (targetEl) textToCopy = targetEl.innerText;
      } else {
        const pre = btn.closest('.code-container')?.querySelector('pre:not([style*="display: none"])');
        if (pre) textToCopy = pre.innerText;
      }

      if (textToCopy) {
        navigator.clipboard.writeText(textToCopy).then(() => {
          const orig = btn.innerText;
          btn.innerText = 'Copied!';
          btn.style.color = '#38bdf8';
          btn.style.borderColor = '#38bdf8';
          setTimeout(() => {
            btn.innerText = orig;
            btn.style.color = '';
            btn.style.borderColor = '';
          }, 2000);
        });
      }
    });
  });

  // Search Filter
  const searchInput = document.getElementById('docSearch');
  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      const query = e.target.value.toLowerCase().trim();
      const articles = document.querySelectorAll('.doc-section');
      
      articles.forEach(art => {
        const text = art.innerText.toLowerCase();
        if (!query || text.includes(query)) {
          art.style.display = '';
        } else {
          art.style.display = 'none';
        }
      });
    });
  }
});
