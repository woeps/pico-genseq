# GitHub Pages Setup Instructions

After pushing the changes to your repository, you need to enable GitHub Pages:

1. Go to your repository on GitHub
2. Click on **Settings** tab
3. Scroll down to **Pages** section in the left sidebar
4. Under "Build and deployment", set:
   - **Source**: GitHub Actions
5. Save the configuration

The first deployment will run automatically when you push to the main/master branch. Subsequent deployments will trigger automatically when any files in the `docs/` directory are changed.

## Accessing Your Documentation

Once deployed, your documentation will be available at:
```
https://woeps.github.io/pico-genseq/
```

## Troubleshooting

If the deployment fails:
1. Check the Actions tab for error details
2. Ensure the workflow file has the correct permissions
3. Verify that the docs directory contains index.html
4. Make sure GitHub Pages is enabled in repository settings
